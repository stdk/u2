#include "api_subway_low.h"
#include "api_subway_high.h"
#include "protocol.h"
#include "commands.h"

#include <iostream>
#include <cstring>

using namespace std;
using namespace boost;

void SerialNumber::fix() {
	if(len >= sizeof(sn)) return;
		
	uint8_t buf[sizeof(sn)] = {0};
	copy(sn,sn+len,buf);
		
	size_t shift = sizeof(sn) - (len + 1);
	memset(sn,0,shift);
	copy(buf,buf + len,&sn[shift]);

	sn[sizeof(sn)-1] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];		
}

bool SerialNumber::operator==(const SerialNumber &other) {
	return memcmp(sn,other.sn,sizeof(this->sn)) == 0;
}

long Card::mfplus_personalize(Reader *reader)
{
	return reader->send_command<SubwayProtocol>(0,MFPLUS_PERSO);
}

long Card::scan(Reader *reader) {
	long ret = request_std(reader);
	if(ret > 0) return ret < ERROR_BASE ? NO_CARD : ret;
	
	ret =  anticollision(reader);
	return ret > 0 && ret < ERROR_BASE ? NO_CARD : ret;
}

long Card::reset(Reader *reader) {
	uint16_t type;
	CHECK(request_std(reader,&type));
	if(this->type != type) return WRONG_CARD;
	SerialNumber sn;
	CHECK(anticollision(reader,&sn));
	return this->sn == sn ? 0 : WRONG_CARD;
}

long Card::request_std(Reader *reader,uint16_t *type) {
	type = type ? type : &this->type;
	return reader->send_command<SubwayProtocol>(0,REQUEST_STD,type);
}

long Card::anticollision(Reader *reader,SerialNumber *sn) {
	sn = sn ? sn : &this->sn;
	long ret = reader->send_command<SubwayProtocol>(0,ANTICOLLISION,sn);
	if((ret & ERR_MASK) == PACKET_DATA_LEN_ERROR) {
		sn->fix();
		return 0;
	} else {
		return ret;
	}
}

long Card::select(Reader *reader) {
	return reader->send_command<SubwayProtocol>(0,SELECT,sn.sn5(),0);
}

/* -------------------------------------------------- */

Sector::Sector(uint8_t _num, uint8_t _key, uint8_t _mode):num(_num),key(_key),mode(_mode) {
	memset(&data,0,sizeof(data));
}

long Sector::authenticate(Reader *reader,Card *card) {
	const int code = this->mode ? AUTH_DYN : AUTH;
	auth_request request = { this->key, this->num, *card->sn.sn5() };
	return reader->send_command<SubwayProtocol>(0,code,&request,(uint8_t*)0);
}

/* ------------------------- */

long Sector::read_block(Reader *reader, uint8_t block, uint8_t enc) {
	if(block >= sizeof(this->data.blocks)/sizeof(block_t)) return -1;

	read_block_request request = { block, this->num, enc };
	return reader->send_command<SubwayProtocol>(0,BLOCK_READ,&request, &this->data.blocks[block]);
}

long Sector::write_block(Reader *reader, uint8_t block, uint8_t enc) {
	if(block >= sizeof(this->data.blocks)/sizeof(block_t)) return -1;

	write_block_request request = { this->data.blocks[block], block, this->num, enc };
	return reader->send_command<SubwayProtocol>(0,BLOCK_WRITE,&request, (uint8_t*)0);
}

/* ------------------------- */

long Sector::read(Reader* reader, uint8_t enc) {
	read_sector_request request = { this->num, enc };
	return reader->send_command<SubwayProtocol>(0,SECTOR_READ,&request,&this->data);
}

long Sector::write(Reader* reader, uint8_t enc) {
	write_sector_request request = { this->data, this->num, enc };
	return reader->send_command<SubwayProtocol>(0,SECTOR_WRITE,&request,(uint8_t*)0);
}

/* ------------------------- */

long Sector::set_trailer(Reader *reader) {
	set_trailer_request request = { this->num, this->key };
	return reader->send_command<SubwayProtocol>(0,SET_TRAILER,&request,(uint8_t*)0);
}

long Sector::set_trailer_dynamic(Reader *reader,Card *card) {
	set_trailer_dynamic_request request = { this->num, this->key, *card->sn.sn5() };
	return reader->send_command<SubwayProtocol>(0,SET_TRAILER_DYN,&request,(uint8_t*)0);
}

/* library interface for card */

EXPORT long card_mfplus_personalize(Reader* reader, Card *card)
{
	return card->mfplus_personalize(reader);
}

EXPORT long card_scan(Reader *reader, Card *card)
{
	return card->scan(reader);
}

EXPORT long card_reset(Reader *reader, Card *card)
{
	return card->reset(reader);
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

EXPORT long card_sector_set_trailer(Reader *reader, Sector *sector)
{
	return sector->set_trailer(reader);
}	

EXPORT long card_sector_set_trailer_dynamic(Reader *reader, Sector *sector, Card *card)
{
	return sector->set_trailer_dynamic(reader,card);
}

