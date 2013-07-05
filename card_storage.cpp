#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/generator_iterator.hpp>
#include <boost/iostreams/device/file.hpp>

#include <ctime>

#include "card_storage.h"
#include "protocol.h"
#include "commands.h"

#include "api_internal.h"

using namespace std;

using boost::iostreams::file_source;
using boost::iostreams::file_sink;

SectorStorage::SectorStorage(uint8_t num, uint8_t key, uint8_t mode):Sector(num,key,mode) {
	enc[0] = enc[1] = enc[2] = 0xFF;
	status = 0;
	memset(pad,0,sizeof(pad));
}

uint64_t CardStorage::generate_random_sn() {
	typedef boost::mt19937 base_gen_type;
	typedef boost::uniform_int<uint64_t> distribution_type;
	typedef boost::variate_generator<base_gen_type&, distribution_type> gen_type;

	base_gen_type base_gen(static_cast<unsigned int>(std::time(0)));
	distribution_type distribution(1,(((uint64_t)1) << 64) - 1);
	gen_type gen(base_gen,distribution);
	uint64_t random_sn = gen();
	fprintf(stderr,"random sn: %llX\n",random_sn);

	return random_sn;		
}

CardStorage::CardStorage(const char *path) {
	unused = 0;

	if(path && load(path) == 0) return;

	sn = generate_random_sn();

	for(size_t i = 0; i < sizeof(sectors)/sizeof(*sectors); i++) {
		sectors[i].num = i;
	}
}

long CardStorage::load(const char *path) {
	file_source src(path,BOOST_IOS::binary);
	streamsize bytes_read = src.read((char*)this,sizeof(*this));
	long ret = bytes_read != sizeof(*this);
	fprintf(stderr,"FileImpl load[%s] -> [%i][%s]\n",path,bytes_read,ret ? "FAIL" : "OK");
	return ret;
}

long CardStorage::save(const char *path) {
	file_sink dst(path,BOOST_IOS::binary);
	streamsize bytes_written = dst.write((char*)this,sizeof(*this));
	long ret = bytes_written != sizeof(*this);
	fprintf(stderr,"FileImpl save[%s] -> [%i][%s]\n",path,bytes_written,ret ? "FAIL" : "OK");
	return ret;
}

/*
def clear(card,sectors):
 def clear_sector(num,key,mode):
  try:
   sector = card.sector(num=num,key=key,mode=mode,method='full',read = False)
   sector.write()
   if key: sector.set_trailer(0,mode='static')
   return True
  except (SectorReadError,SectorWriteError):
   card.reset()
   if key: return clear_sector(num,0,'static')
   else: raise
 [clear_sector(*args) for args in sectors]

[(1,2,s),(2,3,s),(3,7,s),(4,7,s),(5,6,s),(9,4,s),(10,5,s),(11,8,s)]
*/

struct sector_access
{
	uint8_t num;
	uint8_t key;
	uint8_t mode;
	uint8_t sector_enc; //if this is equals to 0, then by-block reading should be used with block_enc
	uint8_t block_enc[3];
};

static long card_sector_auth_tenacious(Reader* reader,Card *card,Sector *sector)
{
	long ret = sector->authenticate(reader,card);
	if(ret || sector->read_block(reader,0,0xFF)) {
		if(sector->key) {
			sector->key = 0;
			return card_sector_auth_tenacious(reader,card,sector);
		} else {
			return ERROR_READ;
		}		
	}

	return 0;
}

long Reader::save(const char *path)
{
	if(!impl) return NO_IMPL;

	ISaveLoadable *save_load = dynamic_cast<ISaveLoadable*>(impl);
	if(save_load) return save_load->save(path);

	Card card;
	long ret = card.scan(this);
	if(ret) return ret;

	sector_access access[] = {
		{ 1, 2, Sector::STATIC , 0xFF},
		{ 2, 3, Sector::STATIC , 0xFF},
		{ 3, 7, Sector::STATIC , 0xFF},
		{ 4, 7, Sector::STATIC , 0xFF},
		{ 5, 6, Sector::STATIC , 0xFF},
		{ 9, 4, Sector::STATIC , 0xFF},
		{10, 5, Sector::STATIC , 0xFF},
		{11, 8, Sector::STATIC ,    0, {0xFF, 0xA, 0xA} },
		{13,27, Sector::DYNAMIC,    3, },
		{14,27, Sector::DYNAMIC,    0, { 0x3, 0x3,   0} }
	};

	CardStorage storage;
	for(size_t i = 0; i < sizeof(access)/sizeof(*access); i++) {
		sector_access *A = access + i;

		Sector *sector = storage.sectors + A->num;
		sector->num = A->num;
		sector->key = A->key;
		sector->mode = A->mode;
		CHECK(card_sector_auth_tenacious(this,&card,sector));

		//if any of enc modes != 0xFF then we should use by-block reading
		if(A->sector_enc) {
			sector->read(this,A->sector_enc);
		} else {
			for(uint8_t block = 0; i < 3; i++) {
				sector->read_block(this,block,A->block_enc[block]);
			}
		}
		
	}

	return NO_IMPL_SUPPORT;	
}

long Reader::load(const char *path)
{
	if(!impl) return NO_IMPL;

	ISaveLoadable *save_load = dynamic_cast<ISaveLoadable*>(impl);
	if(save_load) return save_load->load(path);

	return NO_IMPL_SUPPORT;
}