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

struct SerialNumber
{
	uint8_t sak;
	uint8_t len;

	//   0  1  2  3  4  5  6  7  8  9 10
	//[ 10 10 10 10 10 10 10 10 10 10  0 ]
	//[  0  0  0  7  7  7  7  7  7  7  0 ]
	//[  0  0  0  0  0  0  5  5  5  5  5 ]
	uint8_t sn[10 + 1]; //10 byte buffer for sn itself + 1 byte for checksum

	//moves sn bytes to the left depending on its length 
	//to assure correct positioning
	void fix();
	bool operator==(const SerialNumber &other);

	inline SN5* sn5() {
		return (SN5*)&sn[6];
	}
	inline uint8_t* sn7() {
		return &sn[3];
	}
};

class Reader;

struct Card
{
	SerialNumber sn;	
	uint16_t type;	

	long mfplus_personalize(Reader *reader);
	long scan(Reader *reader);
	long reset(Reader *reader);
	long request_std(Reader *reader,uint16_t *type = 0);
	long anticollision(Reader *reader,SerialNumber *sn = 0);
	long select(Reader *reader);
}; // sizeof == 20

// NOTE: operators and methods of sector_t, block_t and SectorBase
// do not perform boundary check, so be careful when using it

struct block_t {
	uint8_t data[BLOCK_LENGTH];

	inline uint8_t& operator[](size_t idx) {
		return data[idx];
	}		
		
	template<typename T, size_t N>
	inline block_t& operator=(T(& ptr)[N]) {
		size_t len = min(sizeof(*this),sizeof(T)*N);
		fprintf(stderr,"A:copying %i bytes\n",len);			
		memcpy(this,ptr,len);
		return *this;
	}

	template<typename T>
	inline block_t& operator=(T& ptr) {
		size_t len = min(sizeof(*this),sizeof(T));
		fprintf(stderr,"B:copying %i bytes\n",len);	
		memcpy(this,&ptr,len);
		return *this;
	}
};

struct sector_t {
	block_t blocks[3];

	inline block_t& operator[](size_t idx) {
		return blocks[idx];
	}

	template<typename T, size_t N>
	inline sector_t& operator=(T(& ptr)[N]) {
		memcpy(this,ptr,min(sizeof(*this),sizeof(T)*N));
		return *this;
	}
};


/*
 Sector class: represents sector on a Mifare Standard card.
 Contains information about sector num, sector key, sector authentication mode and sector data.
 Provides interface to authenticate this sector for a given card and two ways of interacting with
 sector data: either by blocks or as a whole sector.
 sizeof(Sector) == 51
*/
struct Sector
{
	typedef enum {
		STATIC = 0,
		DYNAMIC = 1 
	} auth_mode;

	sector_t data;
	uint8_t num;  // sector number
	uint8_t key;  // index of key for this sector in reader internal memory
	uint8_t mode; // auth_mode

	Sector(uint8_t num, uint8_t key = 0, uint8_t mode = STATIC);

	block_t& operator[](size_t idx) {
		return data.operator [](idx);
	}
	
	/* ------------------------- */

	long authenticate(Reader *reader,Card *card);
	long read_block(Reader *reader, uint8_t block, uint8_t enc);
	long write_block(Reader *reader, uint8_t block, uint8_t enc);
	long read(Reader* reader, uint8_t enc);
	long write(Reader* reader, uint8_t enc);
	long set_trailer(Reader *reader);
	long set_trailer_dynamic(Reader *reader,Card *card);

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