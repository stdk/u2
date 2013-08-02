#include "api_internal.h"
#include "protocol.h"

#define SET_MODE               0x30
#define SET_TIME               0x31
#define GET_TIME               0x32
#define GET_EVENT              0x33
#define GET_LAST_EVENT         0x34
#define REQUEST_WRITE_FILE     0x1e
#define WRITE_FILE             0x1f

#pragma pack(push,1)
struct DEVICE_EVENT {
 uint32_t EventNumber;
 uint32_t DateTime;
 uint32_t DeviceID;
 uint16_t EventCode;
 uint8_t DeviceType; 
 uint8_t DataLen;
 uint8_t EventData[128]; 
};
#pragma pack(pop)

struct request_write_file_t
{
	uint32_t len;
	uint32_t crc;
	uint8_t filename[16];
};


API_FUNCTION_1_TO_0(SET_MODE,proxy_set_mode,uint32_t);
API_FUNCTION_1_TO_0(SET_TIME,proxy_set_time,uint32_t);
API_FUNCTION_0_TO_1(GET_TIME,proxy_get_time,uint32_t);
API_FUNCTION_1_TO_1(GET_EVENT,proxy_get_event,uint32_t,DEVICE_EVENT);
API_FUNCTION_0_TO_1(GET_LAST_EVENT,proxy_get_last_event,uint32_t);
API_FUNCTION_1_TO_1(REQUEST_WRITE_FILE,proxy_request_write_file,request_write_file_t,uint32_t);

EXPORT long proxy_write_file(Reader* reader,void* data, uint8_t len)
{
	return reader->send_command(WRITE_FILE,data,len,0,0);
}
