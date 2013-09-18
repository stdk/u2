#ifndef SUBWAY_PROTOCOL
#define SUBWAY_PROTOCOL

#include "protocol.h"

#define FBGN        0xFF
#define FESC        0xF1
#define TFBGN       0xF2
#define TFESC       0xF3

#define ACK_BYTE	0x00
#define NACK_BYTE	0x01

#define CRC_LEN		2

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
long create_custom_packet(void *packet,size_t max_packet_len,uint8_t code,void *data,uint8_t len);

size_t unbytestaff(void* dst_buf,size_t dst_len,void *src_buf,size_t src_len,bool wait_for_fbgn = true);
size_t bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len);

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

class Unbytestaffer
{
	uint8_t buffer[1024];
	uint8_t *sink;
	bool wait_for_fbgn;
	bool escape;
public:
	Unbytestaffer();

    void reset();

	size_t feed(void *data, size_t len);

	template<typename T>
	inline T get() {
		return (T)buffer;
	}

	inline size_t size() const {
		return sink - buffer;
	}	
};

class SubwayProtocol : public Protocol
{
	IOProvider *provider;
	function<void ()> disconnect;

	Unbytestaffer filter;

	uint8_t write_buf[1024];

	void timeout();

	// Receives block of data that should be parsed according to protocol tules.
	// Return values: 
	// -1 -> data cannot be parsed as a protocol packet;
	//  0 -> there is not enough data to make decision about packet;
	//  1 -> packet has been successfully formed from data given so far;
	long feed(void *data, size_t len);

	long write_callback(size_t bytes_transferred, size_t bytes_sent_to_transfer,const system::error_code &error);

	void set_answer(ProtocolAnswer answer);
public:
	SubwayProtocol(IOProvider *_provider);
	virtual ~SubwayProtocol();

	virtual long send(uint8_t code, void *data, size_t len);	
};

#endif