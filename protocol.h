#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <boost/cstdint.hpp>
#include <boost/thread/future.hpp>
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
using namespace boost;

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


#define FBGN        0xFF
#define FESC        0xF1
#define TFBGN       0xF2
#define TFESC       0xF3

#define ACK_BYTE	0x00
#define NACK_BYTE	0x01

#define CRC_LEN		2

size_t unbytestaff(void* dst_buf,size_t dst_len,void *src_buf,size_t src_len,bool wait_for_fbgn = true);
size_t bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len);
void debug_data(const char* header,void* data,size_t len);

#pragma pack(push,1)
struct PacketHeader
{
	//returns full packet size using information from header
	size_t full_size();

	//Warning: this method assumes that `this` points 
	//to a buffer of at least this->full_size() length
	//to be able to check correctness of last 2 bytes
	//without access violation.
	bool crc_check();

	//Warning: this function assumes, that `this` points 
	//to buffer of at least this->full_size() length.
	//If code == NACK_BYTE, it means error happened
	//and information about that error came in packet data.	
	//No more that 4 bytes of nack data can be retrieved this way.
	inline uint32_t nack_data();

	//Warning: this function assumes, that `this` points 
	//to buffer of at least this->full_size() length.
	//Copies data contents of this packet to given buffer.
	size_t get_data(void* buf,size_t len);

	//Returns pointer to data section of this packet or 0
	//if there is no data in packet.
	uint8_t* data();

	uint8_t head;
	uint8_t addr;
	uint8_t code;
	uint8_t len;
};
#pragma pack(pop)

uint16_t prepare_packet(uint8_t code,void *packet,size_t packet_len);

#pragma pack(push,1)
template<typename T>
struct Packet
{
	Packet(uint8_t code,T *data=0) {
		this->header.len = sizeof(T);
		if(data) this->data = *data;
		this->crc = prepare_packet(code,this,sizeof(*this));
	}

	PacketHeader header;
	T data;
	uint16_t crc;
};
#pragma pack(pop)

// Parameters:
// void* packet - buffer for packet being constructed
// size_t max_packet_len - Length of packet buffer. Maximal possible length of constructed packet.
// uint8_t code - code of packet being constructed
// void *data - payload of packet being constructed
// uint8_t len - length of data buffer
//
// Return value:
// -1 when there is not enough space in given buffer for complete packet;
// length of successfully constructed packet otherwise
long create_custom_packet(void* packet,size_t max_packet_len,uint8_t code,void *data,uint8_t len);

#pragma pack(push,1)
struct EmptyPacket
{
	EmptyPacket(uint8_t code) {
		this->header.len = 0;
		this->crc = prepare_packet(code,this,sizeof(*this));
	}

	PacketHeader header;
	uint16_t crc;
};
#pragma pack(pop)

class IOProvider
{
public:
	// write_callback return value specifies the course of action, that IOProvider should take: 
	//  0 -> everything is ok, IOProvider should initiate reading after that;
	// -1 -> write failed, IOProvider should not initiate reading.
	typedef function<long (size_t bytes_transferred, const system::error_code&)> send_callback;
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
	void *data;
	size_t len;

	ProtocolAnswer(void *_data, size_t _len);
	ProtocolAnswer(long _result);
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
	virtual void send(void *data, size_t len) = 0;

	// This method returns results of protocol work.
	// It blocks calling thread until there is complete packet in its buffer.
	ProtocolAnswer get_answer();

protected:
	// This method can be used to set answer externally, to make get_answer
	// return prematurely. (e.g. in timeout callback).
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
public:
	Reader(const char* path, uint32_t baud,const char* impl_tag);
	~Reader();

	template<class Request,class Answer>
	inline long send_command(int code,Request *request,Answer *answer = 0,size_t size = sizeof(Answer)) {
		Packet<Request> packet(code,request);
		return send_command(&packet,sizeof(packet),answer,answer ? size : 0);
	}

	template<class Answer>
	inline long send_command(uint8_t code,Answer *answer,size_t size = sizeof(Answer)) {
		EmptyPacket packet(code);
		return send_command(&packet,sizeof(packet),answer,answer ? size : 0);
	}

	inline long send_command(uint8_t code) {
		return send_command(code,(uint8_t*)0);
	}

	long send_command(void* data,size_t len,void* answer,size_t answer_len);

	long save(const char* path);
	long load(const char* path);
};

#endif //PROTOCOL_H