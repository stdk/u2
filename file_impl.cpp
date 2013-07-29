#include <stdio.h>
#include <map>
#include <string>

#include <cstring>
#include <cstdlib>
#include <boost/signals2.hpp>

#include "protocol.h"
#include "card_storage.h"
#include "commands.h"
#include "api_common.h"
#include "custom_combiners.h"

#define CARD_TYPE_STANDARD   0x4
#define CARD_TYPE_ULTRALIGHT 0x44

using namespace std;
using namespace boost;

const int log_level = 0;

class FileImpl : public IOProvider, public ISaveLoadable
{
	typedef uint8_t (FileImpl::*command_handler)(void* in,size_t in_len,void* out,size_t *out_len);
	typedef map<uint8_t,command_handler> HandlerMap;
	HandlerMap handlers;

	CardStorage storage;	

	signals2::signal<long (void *data, size_t len),combiner::maximum<long> > data_received;
public:

	FileImpl(const char* path,uint32_t baud):storage(path) {

		handlers[GET_SN]          = &FileImpl::get_sn;
		handlers[GET_VERSION]     = &FileImpl::get_version;
		handlers[FIELD_ON]        = &FileImpl::field_on;
		handlers[FIELD_OFF]       = &FileImpl::field_off;
		handlers[REQUEST_STD]     = &FileImpl::request_std;
		handlers[ANTICOLLISION]   = &FileImpl::anticollision;
		handlers[AUTH]            = &FileImpl::auth;
		handlers[AUTH_DYN]        = &FileImpl::auth_dyn;
		handlers[BLOCK_READ]      = &FileImpl::block_read;
		handlers[BLOCK_WRITE]     = &FileImpl::block_write;
		handlers[SECTOR_READ]     = &FileImpl::sector_read;
		handlers[SECTOR_WRITE]    = &FileImpl::sector_write;
		handlers[SET_TRAILER]     = &FileImpl::set_trailer;
		handlers[SET_TRAILER_DYN] = &FileImpl::set_trailer_dyn;

	}

	virtual ~FileImpl() {
		
	}

	virtual long load(const char *path) {
		return storage.load(path);
	}

	virtual long save(const char *path) {
		return storage.save(path);
	}

	virtual void send(void *data, size_t len, IOProvider::send_callback callback) 
	{
		uint8_t ret = NO_COMMAND;
		uint8_t request_buf[256] = {0};
		size_t request_buf_len = unbytestaff(request_buf,sizeof(request_buf),data,len);

		callback(len,system::error_code());

		uint8_t data_buf[256] = {0};
		size_t data_buf_len = sizeof(data_buf);
		PacketHeader* header = (PacketHeader*)request_buf;
		if(header->crc_check()) {
			HandlerMap::const_iterator handler = handlers.find(header->code);
			if(handler != handlers.end()) {
				ret = (this->*(handler->second))(header->data(),header->len,data_buf,&data_buf_len);		
			}
		} else {
			ret = CRC_ERROR;
		}
		
		uint8_t packet[256] = {0};
		size_t packet_len = 0;
		if(ret) {
			packet_len = create_custom_packet(packet,sizeof(packet),NACK_BYTE,&ret,sizeof(ret));
		} else {
			packet_len = create_custom_packet(packet,sizeof(packet),header->code,data_buf,data_buf_len);
		}

		uint8_t response[512] = {0};
		size_t response_len = bytestaff(response,sizeof(response),packet,packet_len);

		data_received(response,response_len);
	}

	static void disconnector(signals2::connection c)
	{
		if(log_level) std::cerr << "FileImpl::disconnector" << std::endl;

		c.disconnect();
	}

	function<void ()> listen(IOProvider::listen_callback callback)
	{
		if(log_level) std::cerr << "FileImpl::listen" << std::endl;

		signals2::connection c = data_received.connect(callback);
		return bind(disconnector,c);
	}

	virtual long set_timeout(size_t time, function<void ()> callback)
	{
		if(log_level) std::cerr << "FileImpl::set_timeout" << std::endl;
		return 0;
	}

	virtual long cancel_timeout()
	{
		if(log_level) std::cerr << "FileImpl::cancel_timeout" << std::endl;
		return 0;
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
		static const char version[7] = "F01";
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
		static const uint16_t type = CARD_TYPE_STANDARD;
		return make_answer(type,out,out_len);
	}

	uint8_t anticollision(void* in,size_t in_len,void* out,size_t *out_len) {
		const size_t sn_len = 7;
		uint8_t answer[2 + sn_len] = {0, sn_len };
		memcpy(&answer[2],&storage.sn,sn_len);
		
		*out_len = min(*out_len,sizeof(answer));
		memcpy(out,answer,*out_len);
		return 0;
	}	

	void clear_card_auth() {
		for(int i = 0;i < sizeof(storage.sectors)/sizeof(SectorStorage); i++) {
			storage.sectors[i].status = SectorStorage::NO_AUTH;
		}
	}

	uint8_t auth(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::auth_request *request = (Sector::auth_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;
			
		clear_card_auth();

		SectorStorage* sector = storage.sectors + request->sector;

		//fprintf(stderr,"sector[%i] mode[%i]\n",request->sector,sector->mode);
		if(sector->mode == SectorStorage::STATIC && sector->key == request->key) {
			sector->status = SectorStorage::AUTHENTICATED;
			//fprintf(stderr,"sector[%i] status[%i]\n",request->sector,sector->status);
		}

		*out_len = 0;
		return 0;
	}

	uint8_t auth_dyn(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::auth_request *request = (Sector::auth_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;

		clear_card_auth();

		SectorStorage* sector = storage.sectors + request->sector;
		if(sector->mode == SectorStorage::DYNAMIC && sector->key == request->key) {
			sector->status = SectorStorage::AUTHENTICATED;
			//fprintf(stderr,"sector[%i] status[%i]\n",request->sector,sector->status);
		}

		*out_len = 0;
		return 0;
	}

	uint8_t block_read(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::read_block_request *request = (Sector::read_block_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;
		if(request->block >= sizeof(sector_t)/sizeof(block_t)) return ERROR_VALUE;

		SectorStorage* sector = storage.sectors + request->sector;
		//fprintf(stderr,"key[%i] status[%i]\n",sector->key,sector->status);
		if(!sector->status) return ERROR_READ;
		if(request->enc != sector->enc[request->block]) return ERROR_READ;
		
		return make_answer((*sector)[request->block],out,out_len);		
	}

	uint8_t block_write(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::write_block_request *request = (Sector::write_block_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;
		if(request->block >= sizeof(sector_t)/sizeof(block_t)) return ERROR_VALUE;
		
		SectorStorage* sector = storage.sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;
		
		sector->enc[request->block] = request->enc;
		(*sector)[request->block] = request->data;
		
		*out_len = 0;
		return 0;
	}

	uint8_t sector_read(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::read_sector_request *request = (Sector::read_sector_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;

		SectorStorage* sector = storage.sectors + request->sector;
		if(!sector->status) return ERROR_READ;
		if(request->enc != sector->enc[0]) return ERROR_READ;

		return make_answer(storage.sectors[request->sector].data,out,out_len);
	}

	uint8_t sector_write(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::write_sector_request *request = (Sector::write_sector_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;
		
		SectorStorage* sector = storage.sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;
		
		sector->enc[0] = request->enc;
		sector->data = request->data;
		
		*out_len = 0;
		return 0;
	}

	uint8_t set_trailer(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::set_trailer_request *request = (Sector::set_trailer_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;

		SectorStorage* sector = storage.sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;
		
		sector->mode = SectorStorage::STATIC;
		sector->key = request->key;

		*out_len = 0;
		return 0;
	}

	uint8_t set_trailer_dyn(void* in,size_t in_len,void* out,size_t *out_len) {
		Sector::set_trailer_dynamic_request *request = (Sector::set_trailer_dynamic_request*)in;
		if(request->sector >= sizeof(storage.sectors)/sizeof(SectorStorage)) return ERROR_VALUE;

		SectorStorage* sector = storage.sectors + request->sector;
		if(!sector->status) return ERROR_WRITE;

		sector->mode = SectorStorage::DYNAMIC;
		sector->key = request->key;

		*out_len = 0;
		return 0;
	}
};

IOProvider* create_file_impl(const char* path,uint32_t baud)
{
	return new FileImpl(path,baud);
}