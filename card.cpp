#include "api_internal.h"
#include "api_common.h"
#include "protocol.h"
#include "commands.h"

#include <iostream>
#include <cstring>

using namespace std;
using namespace boost;

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
	void fix() {
		if(len >= sizeof(sn)) return;
		
		uint8_t buf[sizeof(sn)] = {0};
		copy(sn,sn+len,buf);
		
		size_t shift = sizeof(sn) - (len + 1);
		memset(sn,0,shift);
		copy(buf,buf + len,&sn[shift]);

		sn[sizeof(sn)-1] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];		
	}	

	//uint64_t getSN64() { return *(uint64_t*)sn; }
	inline SN5* sn5() {
		return (SN5*)&sn[6];
	}
	inline uint8_t* sn7() {
		return &sn[3];
	}
};

struct Card
{
	SerialNumber sn;	
	uint16_t type;	

	inline long scan(Reader *reader) {
		CHECK(request_std(reader));
		return anticollision(reader);
	}

	inline long reset(Reader *reader) {
		return request_std(reader);
	}

	inline long request_std(Reader *reader) {
		return reader->send_command(REQUEST_STD,&type);
	}

	inline long anticollision(Reader *reader) {
		long ret = reader->send_command(ANTICOLLISION,&sn);
		if((ret & ERR_MASK) == PACKET_DATA_LEN_ERROR) {
			sn.fix();
			return 0;
		} else {
			return ret;
		}
	}

	inline long select(Reader *reader) {
		return reader->send_command(SELECT,sn.sn5(),0);
	}
}; // sizeof == 20

/*
 Sector class: represents sector on a Mifare Standard card.
 Contains information about sector num, sector key, sector authentication mode and sector data.
 Provides interface to authenticate this sector for a given card and two ways of interacting with
 sector data: either by blocks or as a whole sector.
 sizeof(Sector) == 51
*/
struct Sector : public SectorBase
{
	inline long authenticate(Reader *reader,Card *card) {
		const int code = this->mode ? AUTH_DYN : AUTH;
		auth_request request = { this->key, this->num, *card->sn.sn5() };
		return reader->send_command(code,&request,(uint8_t*)0);
	}

	/* ------------------------- */

	inline long read_block(Reader *reader, uint8_t block, uint8_t enc) {
		if(block >= sizeof(this->data.blocks)/sizeof(block_t)) return -1;

		read_block_request request = { block, this->num, enc };
		return reader->send_command(BLOCK_READ,&request, &this->data.blocks[block]);
	}

	inline long write_block(Reader *reader, uint8_t block, uint8_t enc) {
		if(block >= sizeof(this->data.blocks)/sizeof(block_t)) return -1;

		write_block_request request = { this->data.blocks[block], block, this->num, enc };
		return reader->send_command(BLOCK_WRITE,&request, (uint8_t*)0);
	}

	/* ------------------------- */

	inline long read(Reader* reader, uint8_t enc) {
		read_sector_request request = { this->num, enc };
		return reader->send_command(SECTOR_READ,&request,&this->data);
	}

	inline long write(Reader* reader, uint8_t enc) {
		write_sector_request request = { this->data, this->num, enc };
		return reader->send_command(SECTOR_WRITE,&request,(uint8_t*)0);
	}

	/* ------------------------- */

	inline long set_trailer(Reader* reader) {
		set_trailer_request request = { this->num, this->key };
		return reader->send_command(SET_TRAILER,&request,(uint8_t*)0);
	}

	inline long set_trailer_dynamic(Reader* reader,Card * card) {
		set_trailer_dynamic_request request = { this->num, this->key, *card->sn.sn5() };
		return reader->send_command(SET_TRAILER,&request,(uint8_t*)0);
	}
};


/* library interface for card */

EXPORT long card_scan(Reader* reader, Card *card)
{
	return card->scan(reader);
}

EXPORT long card_sector_auth(Reader *reader, Card *card, Sector *sector)
{
	return sector->authenticate(reader, card);
}

EXPORT long card_sector_read(Reader *reader,Sector *sector,uint8_t enc)
{
	return sector->read(reader,enc);
}

EXPORT long card_sector_write(Reader *reader,Sector *sector,uint8_t enc)
{
	return sector->write(reader,enc);
}

EXPORT long card_block_read(Reader *reader, Sector *sector, uint8_t block, uint8_t enc)
{
	return sector->read_block(reader, block, enc);	
}

EXPORT long card_block_write(Reader *reader, Sector *sector,uint8_t block, uint8_t enc)
{
	return sector->write_block(reader, block, enc);	
}

EXPORT long card_sector_set_trailer(Reader* reader, Sector *sector)
{
	return sector->set_trailer(reader);
}	

EXPORT long card_sector_set_trailer_dynamic(Reader* reader, Sector *sector, Card *card)
{
	return sector->set_trailer_dynamic(reader,card);
}

