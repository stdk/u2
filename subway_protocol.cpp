#include "subway_protocol.h"

#include "api_subway_low.h"
#include "crc16.h"

#include <string>
#include <cstring>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/scoped_array.hpp>


using namespace boost;

static const size_t TIMEOUT = 1500;
static const int log_level = getenv("DEBUG_SUBWAY_PROTOCOL") != 0;

size_t PacketHeader::full_size() const {
	return sizeof(*this) + this->len + CRC_LEN;
}

bool PacketHeader::crc_check() const {
	size_t len = this->full_size() - CRC_LEN;
	CRC16_Calc(this,len);
	uint8_t *p = (uint8_t*)this;
	return p[len] == CRC16_Low && p[len+1] == CRC16_High;
}

uint32_t PacketHeader::nack_data() const {
	uint32_t result = 0;
	get_data(&result,sizeof(result));		
	return result;
}

uint8_t* PacketHeader::data() const {
	return (uint8_t*)this + sizeof(*this);
}

size_t PacketHeader::get_data(void *buf,size_t len) const {
	size_t copy_len = std::min((size_t)this->len,len);
	memcpy(buf,data(),copy_len);
	return copy_len;
}

uint16_t prepare_packet(uint8_t addr, uint8_t code, void *packet, size_t packet_len) {
	PacketHeader* header = (PacketHeader*)packet;
	header->head = FBGN;
	header->addr = addr;
	header->code = code;

	CRC16_Calc(packet,packet_len - CRC_LEN);

    uint8_t *packet_u8 = (uint8_t*)packet;
	packet_u8[packet_len - 1] = CRC16_High;
	packet_u8[packet_len - 2] = CRC16_Low;

	return (CRC16_High << 8) + CRC16_Low;
}

long create_custom_packet(void *packet, size_t max_packet_len,
						  uint8_t addr, uint8_t code,
						  void *data, uint8_t len) {
	if(max_packet_len < sizeof(PacketHeader)) return -1;

	PacketHeader *header = (PacketHeader*)packet;
	header->len = len;
	
	size_t packet_len = header->full_size();
	if(max_packet_len < packet_len) return -1;
#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4200 )
#endif
	struct CustomPacket {
		PacketHeader header;
		uint8_t contents[0];
	} *custom_packet = (CustomPacket*)packet;
#ifdef WIN32
#pragma warning( pop )
#endif

	memcpy(custom_packet->contents,data,len);
	prepare_packet(addr,code,packet,packet_len);	

	return packet_len;
}

size_t unbytestaff(void *dst_buf,size_t dst_len,void *src_buf,size_t src_len, bool wait_for_fbgn) {
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

size_t bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len) {
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
	
	return dst - (uint8_t*)dst_buf;
}


EXPORT long bytestaffing_test(uint8_t *data,size_t len) {
	//debug_data("data_in",data,len);

	scoped_array<uint8_t> bs(new uint8_t[len*2]);
	size_t bs_len = bytestaff(bs.get(),len*2,data,len);
	//debug_data("data_bs",bs.get(),bs_len);	

	scoped_array<uint8_t> un_bs(new uint8_t[len]);
	size_t un_bs_len = unbytestaff(un_bs.get(),len,bs.get(),bs_len,false);
	//debug_data("data_un",un_bs.get(),un_bs_len);

	return un_bs_len == len ? memcmp(un_bs.get(),data,len) : -1;
}

/*
#pragma pack(push,1)
template<typename T>
struct Packet
{
	Packet(uint8_t code,T *data=0) {
		this->header.len = sizeof(T);
		if(data) this->data = *data;
		this->crc = prepare_packet(code,this,sizeof(*this));
	}

	PacketHeader header;
	T data;
	uint16_t crc;
};
#pragma pack(pop)

#pragma pack(push,1)
struct EmptyPacket
{
	EmptyPacket(uint8_t code) {
		this->header.len = 0;
		this->crc = prepare_packet(code,this,sizeof(*this));
	}

	PacketHeader header;
	uint16_t crc;
};
#pragma pack(pop)
*/


SubwayUnbytestaffer::SubwayUnbytestaffer() {
	reset();
}

size_t SubwayUnbytestaffer::feed(void *data, size_t len) {
	if(!data || !len) return 0;

	uint8_t *src = (uint8_t*)data;
	uint8_t *src_end = src + len;
	uint8_t *dst = sink;
	uint8_t *dst_end = buffer + sizeof(buffer);

	if(wait_for_fbgn) {
		while(src != src_end && *src != FBGN && src++);
		if(src != src_end) wait_for_fbgn = false;
	}

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
	size_t bytes_parsed = dst - sink;
	sink = dst;
	return bytes_parsed;
}

void SubwayUnbytestaffer::reset() {
	sink = buffer;
	wait_for_fbgn = true;
	escape = false;
}


SubwayProtocol::SubwayProtocol(IOProvider *_provider):provider(_provider) {
	disconnect = provider->listen(bind(&SubwayProtocol::feed,this,_1,_2));
}

SubwayProtocol::~SubwayProtocol() {
	if(!disconnect.empty()) disconnect();
}

// This method will be used as callback in IOProvider->set_timeout.
// Fires when maximum packet waiting time expired. If its called, that means not 
// enough data came from the serial port to be recognized as a complete packet by read callbacks.
void SubwayProtocol::timeout() {
	if(log_level) std::cerr << "SubwayProtocol::timeout" << std::endl;
	set_answer(ProtocolAnswer(NO_ANSWER));
}

long SubwayProtocol::write_callback(size_t bytes_sent_to_transfer, size_t bytes_transferred,
									const system::error_code &error) {
	if (error)
	{
		std::cerr << "write_callback error:" << error << ": " << error.message() << std::endl;
		set_answer(ProtocolAnswer(IO_ERROR));
		return -1;
	}

	if(log_level) std::cerr << "write_callback: " << bytes_transferred << "/" << bytes_sent_to_transfer << std::endl;

	provider->set_timeout(TIMEOUT,bind(&SubwayProtocol::timeout,this));

	return 0;
}

long SubwayProtocol::send(uint8_t addr, uint8_t code, void *data, size_t len) {
	uint8_t packet[256] = {0};
	long packet_len = create_custom_packet(packet,sizeof(packet),addr,code,data,len);
	if(packet_len == -1) {
		std::cerr << "create_custom_packet failed for command code: " << code << std::endl;
		return -0xCF;
	}

	size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),packet,packet_len);

	if(log_level) debug_data("send",write_buf,write_buf_len);

	provider->send(write_buf,write_buf_len,
		bind(&SubwayProtocol::write_callback,this,write_buf_len,_1,_2));

	return 0;
}

void SubwayProtocol::set_answer(ProtocolAnswer answer) {
	if(!disconnect.empty()) {
		disconnect();
		disconnect.clear();
	}
	provider->cancel_timeout();
	Protocol::set_answer(answer);
}

long SubwayProtocol::feed(void *data, size_t len) {
	if(log_level) debug_data("feed",data,len);

	if(!data || !len) return 0;

	filter.feed(data,len);

	if(filter.size() < sizeof(PacketHeader)) return 0;

	PacketHeader *header = filter.get<PacketHeader*>();

	if(filter.size() < header->full_size()) return 0; //not enough data

	if(!header->crc_check()) {
		set_answer(ProtocolAnswer(PACKET_CRC_ERROR));
	} else if(header->code == NACK_BYTE) {
		set_answer(ProtocolAnswer(header->nack_data(),header->addr,header->code));
	} else {
		set_answer(ProtocolAnswer(header->data(),header->len,header->addr,header->code));
	}

	return 1;
}