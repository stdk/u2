#include "api_internal.h"
#include "protocol.h"

#define SET_MODE               0x30
#define SET_TIME               0x31
#define GET_TIME               0x32
#define GET_EVENT              0x33
#define GET_LAST_EVENT         0x34

#pragma pack(push,1)
struct DEVICE_EVENT {
 uint32_t EventNumber;
 uint32_t DateTime;
 uint32_t DeviceID;
 uint8_t DeviceType;
 uint8_t EventCode;
 uint8_t ErrorCode;
 uint8_t DataLen;
 uint8_t EventData[128]; 
};
#pragma pack(pop)

char t[144-sizeof(DEVICE_EVENT)];

API_FUNCTION_1_TO_0(SET_MODE,proxy_set_mode,uint32_t);
API_FUNCTION_1_TO_0(SET_TIME,proxy_set_time,uint32_t);
API_FUNCTION_0_TO_1(GET_TIME,proxy_get_time,uint32_t);
API_FUNCTION_1_TO_1(GET_EVENT,proxy_get_event,uint32_t,DEVICE_EVENT);
API_FUNCTION_0_TO_1(GET_LAST_EVENT,proxy_get_last_event,uint32_t);
