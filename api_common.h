#ifndef API_COMMON
#define API_COMMON

#include <boost/cstdint.hpp>
using namespace boost;

#define BLOCK_LENGTH 16

#pragma pack(push,1)
struct SN5
{
	uint8_t sn[5];
};
#pragma pack(pop)

#endif