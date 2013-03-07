#include "protocol.h"
#include "commands.h"
#include "api_common.h"

#include <stdio.h>
#include <map>

#include <cstring>
#include <cstdlib>

using namespace std;

struct Sector : public SectorBase
{
	 enum auth_status {
		NO_AUTH = 0,
		AUTHENTICATED = 1
	};

	uint8_t enc[3]; // encryption keys for every block, when reading or writing whole sector enc[0] assumed for it
	uint8_t status; // 0 - no authentication, 1 - authenticated
};

class FileImpl : public IReaderImpl
{
	typedef uint8_t (FileImpl::*command_handler)(void* in,size_t in_len,void* out,size_t *out_len);
	typedef map<uint8_t,command_handler> HandlerMap;
	HandlerMap handlers;

	Sector sectors[16];
public:

	FileImpl(const char* path,uint32_t baud) {
		fprintf(stderr,"FileImpl\n");

		handlers[GET_SN]        = &FileImpl::get_sn;
		handlers[GET_VERSION]   = &FileImpl::get_version;
		handlers[FIELD_ON]      = &FileImpl::field_on;
		handlers[FIELD_OFF]     = &FileImpl::field_off;
		handlers[REQUEST_STD]   = &FileImpl::request_std;
		handlers[ANTICOLLISION] = &FileImpl::anticollision;
		handlers[AUTH]          = &FileImpl::auth;
		handlers[AUTH_DYN]      = &FileImpl::auth_dyn;
		handlers[BLOCK_READ]    = &FileImpl::block_read;
		handlers[BLOCK_WRITE]   = &FileImpl::block_write;
		handlers[SECTOR_READ]   = &FileImpl::sector_read;
		handlers[SECTOR_WRITE]  = &FileImpl::sector_write;
		handlers[SET_TRAILER]   = &FileImpl::set_trailer;

		sectors[11].key = 8;
		sectors[11].enc[0] = 0xFF;
		sectors[11].enc[1] = 0xA;
		sectors[11].enc[2] = 0xA;

		uint8_t x[16] = {0x99,0x41,0x3,0x0,0x5,0x0,0x4d,0x32,0xf1,0xdc,0x1,0xef,0xbc,0xd,0xc3,0xf1};

		sectors[11][0][0] = 0x87;
		sectors[11][1] = sectors[11][2] = x;
	}

	virtual ~FileImpl() {
	
	}

	long transceive(void* data,size_t len,void* packet,size_t packet_len) {
		uint8_t ret = NO_COMMAND;
		uint8_t data_buf[128] = {0};
		size_t data_buf_len = sizeof(data_buf);

		PacketHeader* header = (PacketHeader*)data;
		if(header->crc_check()) {
			HandlerMap::const_iterator handler = handlers.find(header->code);
			if(handler != handlers.end()) {
				ret = (this->*(handler->second))(header->data(),header->len,data_buf,&data_buf_len);		
			}
		} else {
			ret = CRC_ERROR;
		}

		if(ret)	return create_custom_packet(packet,&packet_len,NACK_BYTE,&ret,sizeof(ret));
		return create_custom_packet(packet,&packet_len,header->code,data_buf,data_buf_len);		
	}

	template<typename T>
	inline uint8_t make_answer(T data,void *out, size_t *out_len) {
		*out_len = sizeof(data);
		*(T*)out = data;
		return 0;
	}

	uint8_t get_sn(void* in,size_t in_len,void* out,size_t *out_len) {
		static const uint8_t sn[8] = {1,2,3,4,5,6,7,8};
		*out_len = min(*out_len,sizeof(sn));
		memcpy(out,sn,*out_len);
		return 0;
	}

	uint8_t get_version(void* in,size_t in_len,void* out,size_t *out_len) {
		static const char version[7] = "TEST01";
		*out_len = min(*out_len,sizeof(version));
		memcpy(out,version,*out_len);
		return 0;
	}

	uint8_t field_on(void* in,size_t in_len,void* out,size_t *out_len) {
		*out_len = 0;
		return 0;
	}

	uint8_t field_off(void* in,size_t in_len,void* out,size_t *out_len) {
		*out_len = 0;
		return 0;
	}

	uint8_t request_std(void* in,size_t in_len,void* out,size_t *out_len) {
		static const uint16_t type = 0x44;
		return make_answer(type,out,out_len);
	}

	uint8_t anticollision(void* in,size_t in_len,void* out,size_t *out_len) {
		static const uint8_t answer[] = {0x00,0x04,0xAF,0xBC,0x0E,0xD0};
		*out_len = min(*out_len,sizeof(answer));
		memcpy(out,answer,*out_len);
		return 0;
	}	

	void clear_card_auth() {
		for(int i = 0;i < sizeof(sectors)/sizeof(Sector); i++) {
			sectors[i].status = Sector::NO_AUTH;
		}
	}

	uint8_t auth(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::auth_request *request = (Sector::auth_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;
			
		clear_card_auth();

		Sector* sector = sectors + request->sector;

		fprintf(stderr,"sector[%i] mode[%i]\n",request->sector,sector->mode);
		if(sector->mode == Sector::STATIC && sector->key == request->key) {
			sector->status = Sector::AUTHENTICATED;
		}

		*out_len = 0;
		return 0;
	}

	uint8_t auth_dyn(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::auth_request *request = (Sector::auth_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;

		clear_card_auth();

		Sector* sector = sectors + request->sector;
		if(sector->mode == Sector::DYNAMIC && sector->key == request->key) {
			sector->status = Sector::AUTHENTICATED;
		}

		*out_len = 0;
		return 0;
	}

	uint8_t block_read(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::read_block_request *request = (Sector::read_block_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;
		if(request->block >= sizeof(Sector::sector_t)/sizeof(Sector::block_t)) return ERROR_VALUE;

		Sector* sector = sectors + request->sector;
		fprintf(stderr,"key[%i] status[%i]\n",sector->key,sector->status);
		if(!sector->status) return ERROR_READ;
		if(request->enc != sector->enc[request->block]) return ERROR_READ;
		
		return make_answer((*sector)[request->block],out,out_len);		
	}

	uint8_t block_write(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::write_block_request *request = (Sector::write_block_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;
		if(request->block >= sizeof(Sector::sector_t)/sizeof(Sector::block_t)) return ERROR_VALUE;
		
		Sector* sector = sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;
		
		sector->enc[request->block] = request->enc;
		(*sector)[request->block] = request->data;
		
		*out_len = 0;
		return 0;
	}

	uint8_t sector_read(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::read_sector_request *request = (Sector::read_sector_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;

		Sector* sector = sectors + request->sector;
		if(!sector->status) return ERROR_READ;
		if(request->enc != sector->enc[0]) return ERROR_READ;

		return make_answer(sectors[request->sector].data,out,out_len);
	}

	uint8_t sector_write(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::write_sector_request *request = (Sector::write_sector_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;
		
		Sector* sector = sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;
		
		sector->enc[0] = request->enc;
		sector->data = request->data;
		
		*out_len = 0;
		return 0;
	}

	uint8_t set_trailer(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::set_trailer_request *request = (Sector::set_trailer_request*)in;
		if(request->sector >= sizeof(sectors)/sizeof(Sector)) return ERROR_VALUE;

		Sector* sector = sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;
		sector->key = request->key;

		*out_len = 0;
		return 0;
	}
};

IReaderImpl* create_file_impl(const char* path,uint32_t baud)
{
	return new FileImpl(path,baud);
}