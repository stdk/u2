#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <boost/cstdint.hpp>
#include <boost/thread/future.hpp>
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>

using namespace boost;

namespace PARITY
{
	const uint8_t NONE  = 0;
	const uint8_t ODD   = 1;
	const uint8_t EVEN = 2;
};

#define ERR_MASK                0xFF0000FF

#define ERROR_BASE              0x0A000000

#define NO_CARD                 0x0C000000
#define WRONG_CARD              0x0C0000FF

#define SUCCESS                 0x00000000
#define IO_ERROR                0x0E000001
#define NO_IMPL                 0x0E0000F0
#define NO_IMPL_SUPPORT         0x0E0000F1
#define NO_ANSWER               0x0E0000A0
#define ANSWER_TOO_LONG         0x0E0000AF
#define WRONG_ANSWER            0x0E0000DF
#define PACKET_CRC_ERROR        0x0E0000CC
#define PACKET_DATA_LEN_ERROR   0x0E0000DE

void debug_data(const char* header,void* data,size_t len);

class IOProvider
{
public:
	// write_callback return value specifies the course of action, that IOProvider should take: 
	//  0 -> everything is ok, IOProvider may initiate reading after that;
	// -1 -> write failed, IOProvider should not initiate reading.
	typedef function<long (size_t bytes_transferred, const system::error_code&)> send_callback;
	
	// listen callback notifies user about data that came from IOProvider.
	// IOProvider may possibly interpret return value of this callback
	// as a basis for its next actions: 
	// 0 -> IOProvider should continue reading;
	// 1 -> user has fulfilled its task, no more reading required.
    typedef function<long (void *data, size_t len)> listen_callback;
	typedef function<void ()> timeout_callback;

	virtual void send(void *data, size_t len, send_callback callback) = 0;
	
	//Registers listener for data that comes from IOProvider
	// Return value: callback that disconnects listener.
	virtual function<void ()> listen(listen_callback callback) = 0;

	virtual long set_timeout(size_t timeout, timeout_callback callback) = 0;
	virtual long cancel_timeout() = 0;

	virtual ~IOProvider();
};

struct ProtocolAnswer
{
	long result;
	uint8_t addr;
	uint8_t code;
	void *data;
	size_t len;

	ProtocolAnswer(void *_data, size_t _len, uint8_t addr = 0, uint8_t code = 0);
	ProtocolAnswer(long _result,uint8_t addr = 0, uint8_t code = 0);
};

class Protocol
{
	promise<ProtocolAnswer> answer_promise;
	unique_future<ProtocolAnswer> answer_future;	
public:
	Protocol();
	virtual ~Protocol();

    // initiates protocol operation, called by IOProvider,
	// that provides itself in a parameter
	// Return values:
	// -1 -> protocol startup sequence failed (e.g. provider could not send some data);
	//  0 -> startup sequence succeded
	virtual long send(uint8_t addr, uint8_t code, void *data, size_t len) = 0;

	// This method returns results of protocol work.
	// It blocks calling thread until there is complete packet in its buffer.
	ProtocolAnswer get_answer();

protected:
	// This method can be used to set answer externally, to make get_answer
	// return prematurely. (e.g. due to timeout).
	virtual void set_answer(ProtocolAnswer answer);
};

class ISaveLoadable
{
public:
	virtual ~ISaveLoadable();
	virtual long load(const char *path) = 0;
	virtual long save(const char *path) = 0;
};

class Reader
{
	IOProvider *impl;

	long send_command(Protocol *protocol,uint8_t addr, uint8_t code,
		              void *data, size_t len,void *answer, size_t answer_len);	
public:
	Reader(const char* path, uint32_t baud,uint8_t parity,const char* impl_tag);
	~Reader();

	template<class Proto,class Request,class Answer>
	inline long send_command(uint8_t addr, uint8_t code,
		                     Request *request,Answer *answer = 0,size_t size = sizeof(Answer)) {
		Proto protocol(impl);
		return send_command(&protocol,addr,code,request,sizeof(*request),answer,answer ? size : 0);
	}

	template<class Proto,class Answer>
	inline long send_command(uint8_t addr, uint8_t code,
		                     Answer *answer,size_t size = sizeof(Answer)) {
		Proto protocol(impl);
		return send_command(&protocol,addr,code,0,0,answer,answer ? size : 0);
	}

	template<class Proto>
	inline long send_command(uint8_t addr, uint8_t code) {
		Proto protocol(impl);
		return send_command(&protocol,addr,code,0,0,0,0);
	}

	template<class Proto>
	long send_command(uint8_t addr, uint8_t code,void *data, size_t len,void *answer, size_t answer_len) {
		Proto protocol(impl);
		return send_command(&protocol,addr,code,data,len,answer,answer_len);
	}

	long save(const char* path);
	long load(const char* path);
};

#endif //PROTOCOL_H