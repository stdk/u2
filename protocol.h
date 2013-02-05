#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <boost/cstdint.hpp>
using namespace boost;

#define ERR_MASK                0xFF0000FF

#define IO_ERROR                0x0E000001
#define NO_IMPL                 0x0E0000F0
#define NO_ANSWER               0x0E0000A0
#define WRONG_ANSWER            0x0E0000DF
#define PACKET_CRC_ERROR        0x0E0000CC
#define PACKET_DATA_LEN_ERROR   0x0E0000DE

#define PACKET_RECEIVE_TIMEOUT 3000

#define FBGN        0xFF
#define FESC        0xF1
#define TFBGN       0xF2
#define TFESC       0xF3

#define ACK_BYTE	0x00
#define NACK_BYTE	0x01

#define CRC_LEN		2

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
	uint32_t nack_data();

	//Warning: this function assumes, that `this` points 
	//to buffer of at least this->full_size() length.
	size_t get_data(void* buf,size_t len);

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

long create_custom_packet(void* packet,size_t *packet_len,uint8_t code,void *data,uint8_t len);

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

class IReaderImpl
{
public:
	virtual ~IReaderImpl();
	virtual long transceive(void* data,size_t len,void* packet,size_t packet_len) = 0;
};

class Reader
{
	IReaderImpl *impl;
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

	long send_command(void* packet,size_t packet_len,void* answer,size_t answer_len);
};

#endif //PROTOCOL_H