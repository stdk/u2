#ifndef API_COMMON
#define API_COMMON

#include <cstring>
#include <cstdio>
#include <boost/cstdint.hpp>
using namespace boost;

#define BLOCK_LENGTH 16

#pragma pack(push,1)
struct SN5
{
	uint8_t sn[5];
};
#pragma pack(pop)

struct SectorBase
{
	// NOTE: operators and methods of sector_t, block_t and SectorBase
	// do not perform boundary check, so be careful when using it

	struct block_t {
		uint8_t data[BLOCK_LENGTH];
		uint8_t& operator[](size_t idx) {
			return data[idx];
		}		
		
		template<typename T, size_t N>
		block_t& operator=(T(& ptr)[N]) {
			size_t len = min(sizeof(*this),sizeof(T)*N);
			fprintf(stderr,"A:copying %i bytes\n",len);			
			memcpy(this,ptr,len);
			return *this;
		}

		template<typename T>
		block_t& operator=(T& ptr) {
			size_t len = min(sizeof(*this),sizeof(T));
			fprintf(stderr,"B:copying %i bytes\n",len);	
			memcpy(this,&ptr,len);
			return *this;
		}
	};

	struct sector_t {
		block_t blocks[3];

		block_t& operator[](size_t idx) {
			return blocks[idx];
		}

		template<typename T, size_t N>
		sector_t& operator=(T(& ptr)[N]) {
			memcpy(this,ptr,min(sizeof(*this),sizeof(T)*N));
			return *this;
		}
	};

	typedef enum {
		STATIC = 0,
		DYNAMIC = 1 
	} auth_mode;

	uint8_t num;  // sector number
	uint8_t key;  // index of key for this sector in reader internal memory
	uint8_t mode; // 0 for static authentication, 1 for dynamic authentication	
	sector_t data;

	block_t& operator[](size_t idx) {
		return data.operator [](idx);
	}

	/* ------------------------- */

	struct auth_request {
		uint8_t key;
		uint8_t sector;
		SN5 sn;
	};

	struct read_block_request {
		uint8_t block;
		uint8_t sector;
		uint8_t enc;
	}; //3

	struct write_block_request {
		block_t data;
		uint8_t block;
		uint8_t sector;
		uint8_t enc;
	}; //19

	struct read_sector_request {
		uint8_t sector;
		uint8_t enc;
	}; //2

	struct write_sector_request {
		sector_t data;
		uint8_t sector;
		uint8_t enc;
	}; // 50

	struct set_trailer_request {
		uint8_t sector;
		uint8_t key;
	}; //2

	struct set_trailer_dynamic_request {
		uint8_t sector;
		uint8_t key;
		SN5 sn;
	}; //7
};

#endif