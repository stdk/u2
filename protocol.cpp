#include "protocol.h"

#include <algorithm>

using namespace std;

void debug_data(const char* header,void* data,size_t len)
{
	uint8_t *bytes = (uint8_t*)data;
	fprintf(stderr,"%s: ",header);
	for(size_t i=0;i<len;i++) {
		fprintf(stderr,"%02hhX ",bytes[i]);
	}
	fprintf(stderr,"\n");
}

IOProvider::~IOProvider() 
{

}

ProtocolAnswer::ProtocolAnswer(void *_data, size_t _len, uint8_t _addr, uint8_t _code)
:result(SUCCESS),addr(_addr),code(_code),data(_data),len(_len) 
{

}

ProtocolAnswer::ProtocolAnswer(long _result, uint8_t _addr, uint8_t _code)
:result(_result),addr(_addr),code(_code),data(0),len(0)
{

}

Protocol::Protocol():answer_future(answer_promise.get_future())
{

}	

void Protocol::set_answer(ProtocolAnswer answer)
{
	answer_promise.set_value(answer);
}

ProtocolAnswer Protocol::get_answer()
{
	return answer_future.get();
}

Protocol::~Protocol()
{

}

ISaveLoadable::~ISaveLoadable()
{

}

#ifdef WIN32
IOProvider* create_blockwise_impl(const char *path,uint32_t baud,uint8_t parity);
#else
IOProvider* create_cp210x_impl(const char *path,uint32_t baud,uint8_t parity);
#endif
IOProvider* create_asio_impl(const char *path,uint32_t baud,uint8_t parity);
IOProvider* create_asio_mt_impl(const char *path,uint32_t baud,uint8_t parity);
IOProvider* create_file_impl(const char *path,uint32_t baud,uint8_t parity);
IOProvider* create_tcp_impl(const char *path,uint32_t baud,uint8_t parity);

static IOProvider * get_impl(const char *impl_tag, const char *path, uint32_t baud, uint8_t parity)
{
	std::string s = std::string(impl_tag);
#ifdef WIN32
	if(s == "blockwise") return create_blockwise_impl(path,baud,parity);
#else
	if(s == "cp210x") return create_cp210x_impl(path,baud,parity);
#endif
    if(s == "asio-mt") return create_asio_mt_impl(path,baud,parity);
	if(s == "asio") return create_asio_impl(path,baud,parity);
	if(s == "file") return create_file_impl(path,baud,parity);	
	if(s == "tcp") return create_tcp_impl(path,baud,parity);

	return 0;
}

Reader::Reader(const char *path,uint32_t baud,uint8_t parity,const char *impl_tag):impl(0)
{
	impl = get_impl(impl_tag,path,baud,parity);
}

Reader::~Reader()
{
	delete impl;
}

long Reader::send_command(Protocol *protocol,uint8_t addr, uint8_t code, 
						  void *data, size_t len,void *answer, size_t answer_len)
{
	if(!impl) {
		fprintf(stderr,"NO_IMPL\n");
		return NO_IMPL;	
	}

	//SubwayProtocol protocol(impl);
	
	if(long send_ret = protocol->send(addr,code,data,len)) {
		return send_ret;
	}
		
	ProtocolAnswer protocol_answer = protocol->get_answer();
	if(protocol_answer.result) return protocol_answer.result;
		
	if(answer) {
		size_t copy_len = min(protocol_answer.len,answer_len);
		memcpy(answer,protocol_answer.data,copy_len);		
	}

	if(protocol_answer.len != answer_len) {
		uint16_t payload = (protocol_answer.len << 8) + answer_len;
		return PACKET_DATA_LEN_ERROR | (payload << 8);
	}

	return 0;	 
}
