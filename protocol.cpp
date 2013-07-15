#include "protocol.h"
#include "api_internal.h"
#include "crc16.h"

#include <boost/scoped_array.hpp>

#ifdef WIN32
#include <windows.h>
#endif

#include <time.h>
#include <iostream>
#include <string>
#include <cstring>
#include <algorithm>

using namespace std;

void debug_data(const char* header,void* data,size_t len)
{
	uint8_t *bytes = (uint8_t*)data;
	std::cerr << header << ": ";
	for(size_t i=0;i<len;i++) {
		std::cerr << std::hex << (unsigned int)bytes[i] << " ";
	}
	std::cerr << std::endl;
}

size_t PacketHeader::full_size()
{
	return sizeof(*this) + this->len + CRC_LEN;
}

bool PacketHeader::crc_check()
{
	size_t len = this->full_size() - CRC_LEN;
	CRC16_Calc(this,len);
	uint8_t *p = (uint8_t*)this;
	return p[len] == CRC16_Low && p[len+1] == CRC16_High;
}

uint32_t PacketHeader::nack_data()
{
	uint32_t result = 0;
	get_data(&result,sizeof(result));		
	return result;
}

uint8_t* PacketHeader::data() 
{
	return (uint8_t*)this + sizeof(*this);
}

size_t PacketHeader::get_data(void *buf,size_t len)
{
	size_t copy_len = min((size_t)this->len,len);
	memcpy(buf,data(),copy_len);
	return copy_len;
}

uint16_t prepare_packet(uint8_t code,void *packet,size_t packet_len)
{
	PacketHeader* header = (PacketHeader*)packet;
	header->head = FBGN;
	header->addr = 0;
	header->code = code;

	CRC16_Calc(packet,packet_len - CRC_LEN);

    uint8_t *packet_u8 = (uint8_t*)packet;
	packet_u8[packet_len - 1] = CRC16_High;
	packet_u8[packet_len - 2] = CRC16_Low;

	return (CRC16_High << 8) + CRC16_Low;
}

long create_custom_packet(void *packet,size_t *packet_len,uint8_t code,void *data,uint8_t len)
{
	if(*packet_len < sizeof(PacketHeader)) return -1;

	size_t max_packet_len = *packet_len; //save initial value for comparison
	PacketHeader *header = (PacketHeader*)packet;
	header->len = len;
	
	*packet_len = header->full_size();
	if(max_packet_len < *packet_len) return -1;

	struct CustomPacket {
		PacketHeader header;
		uint8_t contents[0];
	} *custom_packet = (CustomPacket*)packet;

	memcpy(custom_packet->contents,data,len);
	prepare_packet(code,packet,*packet_len);	

	return 0;
}

size_t unbytestaff(void *dst_buf,size_t dst_len,void *src_buf,size_t src_len, bool wait_for_fbgn)
{
	if(!dst_len || !src_len) return 0;

	uint8_t *src = (uint8_t*)src_buf;
	uint8_t *src_end = src + src_len;
	uint8_t *dst = (uint8_t*)dst_buf;
	uint8_t *dst_end = dst + dst_len;

	while(wait_for_fbgn && src != src_end && *src != FBGN && src++);

	//debug_data("src_buf",src,src_end-src);

	bool escape = false;
	while( src != src_end && dst != dst_end ) {
		uint8_t c = *src++;
		if(escape) {
			*dst++ = c == TFBGN ? FBGN : FESC;
			if(c != TFBGN && c != TFESC && dst != dst_end) *dst++ = c; 
			escape = false;
		} else {
			if(c == FESC) escape = true;
			else *dst++ = c;
		}
	}
    return dst - (uint8_t*)dst_buf;
}

size_t bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len)
{
	if(!dst_len) return 0;

	uint8_t *src = (uint8_t*)src_buf;
	uint8_t *src_end = src + src_len;
	uint8_t *dst = (uint8_t*)dst_buf;
	uint8_t *dst_end = dst + dst_len;
	
	*dst++ = *src++;
	while( src != src_end && dst != dst_end ) {
		uint8_t c = *src++;
		if(c == FBGN) { 
			*dst++ = FESC;
			if(dst != dst_end) *dst++ = TFBGN;
		} else if(c == FESC) {
			*dst++ = FESC;
			if(dst != dst_end) *dst++ = TFESC;
		} else *dst++ = c;
	}
	
	return dst - (uint8_t*)dst_buf;;
}


EXPORT long bytestaffing_test(uint8_t *data,size_t len)
{
	//debug_data("data_in",data,len);

	scoped_array<uint8_t> bs(new uint8_t[len*2]);
	size_t bs_len = bytestaff(bs.get(),len*2,data,len);
	//debug_data("data_bs",bs.get(),bs_len);	

	scoped_array<uint8_t> un_bs(new uint8_t[len]);
	size_t un_bs_len = unbytestaff(un_bs.get(),len,bs.get(),bs_len,false);
	//debug_data("data_un",un_bs.get(),un_bs_len);

	return memcmp(un_bs.get(),data,len);
}
#ifdef WIN32
void HandleLastError(const char *msg) {
	DWORD lastError = GetLastError();
	char *err;
    DWORD ret = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			0,
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
			(char*)&err,
			0,
			NULL);
		
	if(!ret) {
		fprintf(stderr,"FormatMessage(%s,%i): %i (%i)\n",msg,lastError,ret,GetLastError());
		return;
	}

	static char buffer[1024];
	_snprintf_s(buffer, sizeof(buffer), "%s(%i): %s\n", msg,lastError,err);
    fprintf(stderr,buffer);
    LocalFree(err);
}

HANDLE ComOpen(const char *path, uint32_t baud,uint32_t flags)
{
	COMMTIMEOUTS		CommTimeouts;
	DCB					dcb;
		
	wchar_t baud_s[50];
	wsprintf(baud_s, L"baud=%d parity=N data=8 stop=1",baud);

	// get a handle to the port
	HANDLE hCom = CreateFileA(path,					// communication port path
				     GENERIC_READ | GENERIC_WRITE,	// read/write types
				     0,								// comm devices must be opened with exclusive access
				     NULL,							// no security attributes
				     OPEN_EXISTING,					// comm devices must use OPEN_EXISTING
				     flags,    	                    // flags
				     0);							// template must be 0 for comm devices

	
	if (hCom == INVALID_HANDLE_VALUE) {
		HandleLastError("CreateFile");
		return hCom;
	}

	// set the timeout values
	CommTimeouts.ReadIntervalTimeout			= 1;
	CommTimeouts.ReadTotalTimeoutMultiplier		= 0;
	CommTimeouts.ReadTotalTimeoutConstant		= 0;
	CommTimeouts.WriteTotalTimeoutMultiplier	= 0;
	CommTimeouts.WriteTotalTimeoutConstant		= 0;

	// configure
	if (SetCommTimeouts(hCom, &CommTimeouts))
	{					   
		if (GetCommState(hCom, &dcb))
		{
			dcb.fOutxCtsFlow = FALSE;
			dcb.fRtsControl = RTS_CONTROL_ENABLE;		// set RTS bit high!
			if (BuildCommDCB(baud_s, &dcb))
			{
				SetCommState(hCom, &dcb);			// normal operation... continue
			}
		}
	}

	return hCom;
}
#endif

IReaderImpl::~IReaderImpl()
{

}

ISaveLoadable::~ISaveLoadable()
{

}

#ifdef WIN32
IReaderImpl* create_blockwise_impl(const char* path,uint32_t baud);
IReaderImpl* create_bytewise_impl(const char* path,uint32_t baud);
#endif
IReaderImpl* create_asio_impl(const char* path,uint32_t baud);
IReaderImpl* create_asio_mt_impl(const char* path,uint32_t baud);
IReaderImpl* create_file_impl(const char* path,uint32_t baud);

static IReaderImpl * get_impl(const char* path, uint32_t baud,const char* impl_tag)
{
	std::string s = std::string(impl_tag);
#ifdef WIN32
	if(s == "bytewise") return create_bytewise_impl(path,baud);
	if(s == "blockwise") return create_blockwise_impl(path,baud);
#endif
    if(s == "asio-mt") return create_asio_mt_impl(path,baud);
	if(s == "asio") return create_asio_impl(path,baud);
	
	if(s == "file") return create_file_impl(path,baud);	

	return 0;
}

Reader::Reader(const char* path,uint32_t baud,const char* impl_tag):impl(0)
{
	impl = get_impl(path,baud,impl_tag);
}

Reader::~Reader()
{
	delete impl;
}

long Reader::send_command(void *packet,size_t packet_len,void *answer,size_t answer_len)
{
	if(!impl) return NO_IMPL;

	uint8_t packet_buf[512] = {0};
	long ret = impl->transceive(packet,packet_len,packet_buf,sizeof(packet_buf));
	if(ret) return ret;

	PacketHeader *header = (PacketHeader*)packet_buf;

    if(!header->crc_check()) return PACKET_CRC_ERROR;
	if(header->code == NACK_BYTE) return header->nack_data();

	if(answer) header->get_data(answer,answer_len);
	if(header-> len != answer_len) {
		uint16_t payload = (header->len << 8) + answer_len;
		return PACKET_DATA_LEN_ERROR | (payload << 8);
	}

	return 0;	
}
