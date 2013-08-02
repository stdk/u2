#include "api_internal.h"

#include "protocol.h"
#include "commands.h"
#include "crc16.h"

#include <iostream>
#include <cstdio>
#include <boost/system/system_error.hpp>

#define PACKET_BUFFER        256
#define MAX_FRAME_SIZE		 128

//#define DEBUG

struct reader_version
{
	char version[7];
};

API_FUNCTION_0_TO_1(GET_SN,reader_get_sn,uint64_t);
API_FUNCTION_0_TO_1(GET_VERSION,reader_get_version,reader_version);
API_SIDE_EFFECT(FIELD_ON,reader_field_on)
API_SIDE_EFFECT(FIELD_OFF,reader_field_off)
API_SIDE_EFFECT(UPDATE_START,reader_update_start)

#pragma pack(push,1)
struct MPCOMMAND
{
	uint16_t shift      :15;
	uint16_t last_frame :1;
};
#pragma pack(pop)

EXPORT long reader_sync(Reader* reader)
{
	MPCOMMAND mp = { 0, 1 };
	return reader->send_command(SYNC_WITH_DEVICE,&mp,(uint8_t*)0);
}

void debug_data(const char* header,void* data,size_t len);

EXPORT long reader_send_package(Reader* reader,uint8_t *data, uint32_t len)
{
#ifdef DEBUG
    fprintf(stderr,"reader_send_package len[%i]\n",len);
#endif

	uint8_t data_buf[sizeof(MPCOMMAND) + MAX_FRAME_SIZE] = {0};
#pragma warning( push )
#pragma warning( disable : 4200 )
	struct mp_package
	{
		MPCOMMAND mp;
		uint8_t data[0];
	} *package = (mp_package*)data_buf;
#pragma warning( pop )
	
	uint8_t data_len = 0;
	uint8_t package_len = 0;

    uint8_t *end = data + len;
	while(data < end)
	{
		data_len = std::min(end - data,MAX_FRAME_SIZE);
		package->mp.last_frame = data_len == MAX_FRAME_SIZE ? 0 : 1;
		
		package_len = data_len + sizeof(*package);
		std::copy(data,data+package_len,package->data);
		
#ifdef DEBUG
		int shift = package->mp.shift;
		int last = package->mp.last_frame;
		fprintf(stderr,"package_len[%i] shift[%i] last[%i]\n",package_len,shift,last);
		fprintf(stderr,"packet len[%i]\n",packet_len);
		debug_data("packet",packet,packet_len);
#endif
		
		long ret = reader->send_command(MULTIBYTE_PACKAGE,package,package_len,0,0);
		
		if(ret) return ret;

		data += data_len;
		package->mp.shift += 1;
	}

	return 0;
}

EXPORT long reader_open(const char *path,uint32_t baud,const char* impl,Reader **reader)
{
	try {
		*reader = new Reader(path,baud,impl);
		return 0;
	} catch(boost::system::system_error& e) {
		std::cerr << e.what() << std::endl;
		return -1;		
	} catch(int& e) {
		return e;
	}
}

EXPORT long reader_close(Reader *reader)
{
	try {
		delete reader;
		return 0;
	} catch(boost::system::system_error& e) {
		std::cerr << e.what() << std::endl;
		return -1;
	} catch(int& e) {
		return e;
	}
}

EXPORT long reader_save(Reader *reader, const char* path)
{
	return reader->save(path);
}

EXPORT long reader_load(Reader *reader, const char* path)
{
	return reader->load(path);
}

EXPORT long crc16_calc(void *data,uint32_t len,uint8_t low_endian)
{
	uint8_t *buffer = (uint8_t*)data;

	CRC16_Calc(buffer,len - 2);
	
	if(low_endian) {
		buffer[len-2] = CRC16_Low;
		buffer[len-1] = CRC16_High;
	} else {
		buffer[len-1] = CRC16_Low;
		buffer[len-2] = CRC16_High;
	}
	return 0;
}

EXPORT long crc16_check(void* data,uint32_t len,uint8_t low_endian)
{
	uint8_t *buffer = (uint8_t*)data;

	CRC16_Calc(buffer,len - 2);

	/*
	for(size_t i = 0; i < len; i++) {
		fprintf(stderr,"%02X ",buffer[i]);
	}
	fprintf(stderr,"[%02X %02X]\n",CRC16_Low,CRC16_High);
	*/
	
	if(low_endian) {
		return (buffer[len-2] == CRC16_Low) && (buffer[len-1] == CRC16_High);
	} else {
		return (buffer[len-1] == CRC16_Low) && (buffer[len-2] == CRC16_High);
	}
}