#include "terminal_protocol.h"

#include <boost/bind.hpp>

using namespace boost;

static const size_t DEFAULT_TIMEOUT = 150;
static const int log_level = getenv("DEBUG_TERMINAL_PROTOCOL") != 0;

static const size_t checksum_length = 2;

struct TerminalPacketHeader
{
	//returns full packet size using provided information about 
	//length of its data section: this function should be used 
	//when new packet is being created.
	//suggest_size(0) returns minimum possible packet size
	static size_t suggest_size(size_t data_len);

	//Warning: this method assumes that `this` points 
	//to a buffer of at least bytes_available size.
	//Returns boolean state of checksum validity.
	bool checksum_check(size_t bytes_available) const;

	//Warning: this method assumes that `this` points 
	//to a buffer of at least bytes_available size.
	//Calculates checksum for a previously filled packet pointed by this.
	uint16_t checksum_calc(size_t bytes_available) const;

	//Warning: this method assumes that `this` points 
	//to a buffer of at least bytes_available size.
	//If type == FNAK, it means error happened
	//and information about that error came in packet data.	
	//No more that 4 bytes of nack data can be retrieved this way.
	uint32_t nack_data(size_t bytes_available) const;

	//Warning: this method assumes that `this` points 
	//to a buffer of at least bytes_available size.
	//Copies data contents of this packet to given buffer.
	size_t get_data(void* buf,size_t len, size_t bytes_available) const;

	//Returns pointer to data section of this packet or 0
	//if there is no data in packet.
	//This pointer should be used together with data_len to 
	//prevent incorrect memory access.
	uint8_t* data() const;

	//Warning: this method assumes that `this` points 
	//to a buffer of at least bytes_available size.
	//Returns length of data section within packet pointed by this.
	size_t data_len(size_t bytes_available) const;

	uint8_t start;
	uint8_t type;
	uint8_t addr;
	uint8_t code;
};

size_t TerminalPacketHeader::suggest_size(size_t data_len) {
	return sizeof(TerminalPacketHeader) + data_len + checksum_length + 1;
}

bool TerminalPacketHeader::checksum_check(size_t bytes_available) const {
	uint8_t *b = (uint8_t*)this;
	uint16_t present_checksum = (b[bytes_available - 3] << 8) + b[bytes_available - 2];

	return present_checksum == checksum_calc(bytes_available);
}

uint16_t TerminalPacketHeader::checksum_calc(size_t bytes_available) const {
	uint8_t *b = (uint8_t*)this;
	
	uint16_t checksum = 0;
	for(size_t i = 1; i < bytes_available - 3; i++) {
		checksum += b[i];
	}

	return checksum;
}

uint32_t TerminalPacketHeader::nack_data(size_t bytes_available) const {
	uint32_t result = 0;
	get_data(&result,sizeof(result),bytes_available);		
	return result;
}

uint8_t* TerminalPacketHeader::data() const {
	return ((uint8_t*)this) + sizeof(*this);
}

size_t TerminalPacketHeader::get_data(void *buf,size_t len, size_t bytes_available) const
{
	size_t copy_len = std::min(data_len(bytes_available),len);
	memcpy(buf,data(),copy_len);
	return copy_len;
}

size_t TerminalPacketHeader::data_len(size_t bytes_available) const {
	return bytes_available - suggest_size(0);
}

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
long terminal_create_custom_packet(void *packet, size_t max_packet_len,
								   uint8_t type, uint8_t addr, uint8_t code,
								   void *data, uint8_t len) {
	size_t packet_len = TerminalPacketHeader::suggest_size(len);

	if(packet_len > max_packet_len) return -1;

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4200 )
#endif
	struct CustomPacket {
		TerminalPacketHeader header;
		uint8_t contents[0];
	} *custom_packet = (CustomPacket*)packet;
#ifdef WIN32
#pragma warning( pop )
#endif

	custom_packet->header.start = FMSTR;
	custom_packet->header.type = type;
	custom_packet->header.addr = addr;
	custom_packet->header.code = code;

	memcpy(custom_packet->contents,data,len);

	uint16_t checksum = custom_packet->header.checksum_calc(packet_len);
	custom_packet->contents[packet_len - sizeof(TerminalPacketHeader) - 3] = checksum >> 8;   // checksum high
	custom_packet->contents[packet_len - sizeof(TerminalPacketHeader) - 2] = checksum & 0xFF; // checksum low
	custom_packet->contents[packet_len - sizeof(TerminalPacketHeader) - 1] = FEND;

	return packet_len;
}

size_t terminal_bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len) {
	if(!dst_len || !src_len) return 0;

	uint8_t *src = (uint8_t*)src_buf;
	uint8_t *src_end = src + src_len - 1;
	uint8_t *dst = (uint8_t*)dst_buf;
	uint8_t *dst_end = dst + dst_len;
	
#define BYTESTAFF_CASE(b)\
case b:\
	*dst++ = FMID;\
	if(dst != dst_end) *dst++ = b;\
	break;

	*dst++ = *src++; //skip first byte
	while( src != src_end && dst != dst_end ) {
		uint8_t c = *src++;
		switch(c) {
		BYTESTAFF_CASE(FSSTR)
		BYTESTAFF_CASE(FMSTR)
		BYTESTAFF_CASE(FEND)
		BYTESTAFF_CASE(FMID)
		default:
			*dst++ = c;
			break;
		}
	}
	if(dst != dst_end) {
		*dst++ = *src++; //skip last byte
	}

#undef BYTESTAFF_CASE
	
	return dst - (uint8_t*)dst_buf;
}

TerminalUnbytestaffer::TerminalUnbytestaffer() {
	reset();
}

size_t TerminalUnbytestaffer::feed(void *data, size_t len) {
	if(!data || !len) return 0;

	uint8_t *src = (uint8_t*)data;
	uint8_t *src_end = src + len;
	uint8_t *dst = sink;
	uint8_t *dst_end = buffer + sizeof(buffer);

	while( src != src_end && dst != dst_end ) {
		uint8_t c = *src++;
		if(escape) {
			escape = false;
			if(!wait_for_start) {
				*dst++ = c;
			}
		} else {
			if(!wait_for_start && c == FEND) {
				*dst++ = c;
				_completed = true;
				break;
			}

			if(c == FMID) {
				escape = true;
			} else {
				if(!wait_for_start) {
					*dst++ = c;
				} else if(c == FSSTR) {
					wait_for_start = false;
					*dst++ = c;
				}
			}
		}
	}
	size_t bytes_parsed = dst - sink;
	sink = dst;
	return bytes_parsed;
}

void TerminalUnbytestaffer::reset() {
	sink = buffer;
	wait_for_start = true;
	_completed = false;
	escape = false;
}

TerminalProtocol::TerminalProtocol(IOProvider *_provider)
:provider(_provider),type(FMAS),timeout(DEFAULT_TIMEOUT) {
	disconnect = provider->listen(bind(&TerminalProtocol::feed,this,_1,_2));
}

TerminalProtocol::~TerminalProtocol() {
	if(!disconnect.empty()) disconnect();
}

// This method will be used as callback in IOProvider->set_timeout.
// Fires when maximum packet waiting time expired. If its called, that means not 
// enough data came from the serial port to be recognized as a complete packet by read callbacks.
void TerminalProtocol::timeout_callback() {
	if(log_level) std::cerr << "TerminalProtocol::timeout_callback" << std::endl;
	set_answer(ProtocolAnswer(NO_ANSWER));
}

long TerminalProtocol::write_callback(size_t bytes_sent_to_transfer, size_t bytes_transferred,
									  const system::error_code &error) {
	if (error)
	{
		std::cerr << "write_callback error:" << error << ": " << error.message() << std::endl;
		set_answer(ProtocolAnswer(IO_ERROR));
		return -1;
	}

	if(log_level) std::cerr << "write_callback: " << bytes_transferred << "/" << bytes_sent_to_transfer << std::endl;

	if(timeout) {
		provider->set_timeout(timeout,bind(&TerminalProtocol::timeout_callback,this));
		return 0;
	} else {
		set_answer(ProtocolAnswer(NO_ANSWER));
		return 1;
	}
}

long TerminalProtocol::send(uint8_t _addr, uint8_t _code, void *data, size_t len) {
	addr = _addr;
	code = _code;

	uint8_t packet[256] = {0};
	long packet_len = terminal_create_custom_packet(packet,sizeof(packet),type,addr,code,data,len);
	if(packet_len == -1) {
		std::cerr << "terminal_create_custom_packet failed for command code: " << code << std::endl;
		return -0xCF;
	}

	size_t write_buf_len = terminal_bytestaff(write_buf,sizeof(write_buf),packet,packet_len);

	if(log_level) debug_data("send",write_buf,write_buf_len);

	provider->send(write_buf,write_buf_len,
		bind(&TerminalProtocol::write_callback,this,write_buf_len,_1,_2));

	return 0;
}

void TerminalProtocol::set_answer(ProtocolAnswer answer) {
	if(!disconnect.empty()) {
		disconnect();
		disconnect.clear();
	}
	provider->cancel_timeout();
	Protocol::set_answer(answer);
}

long TerminalProtocol::feed(void *data, size_t len) {
	if(log_level) debug_data("feed",data,len);

	if(!data || !len) return 0;

	filter.feed(data,len);

	if(!filter.completed()) return 0; 

	TerminalPacketHeader *header = filter.get<TerminalPacketHeader*>();
	size_t full_size = filter.size();

	//when we have an answer from another device
	if(header->addr != addr) {
		fprintf(stderr,"header->addr[%hhX] != addr[%hhX]",header->addr,addr);
		debug_data("",header,full_size);
		filter.reset();
		return 0;
	}

	//when we have an answer to another command
	if(header->code != code) {
		fprintf(stderr,"header->code[%hhX] != code[%hhX]",header->code,code);
		debug_data("",header,full_size);
		filter.reset();
		return 0;
	}

	if(full_size < header->suggest_size(0)) {
		//when we have completed packet that consists of less bytes than minimal one
		//its obviously something wrong
		set_answer(ProtocolAnswer(WRONG_ANSWER));
	} else if(!header->checksum_check(full_size)) {
		set_answer(ProtocolAnswer(PACKET_CRC_ERROR));
	} else if(header->type == FNAK) {
		set_answer(ProtocolAnswer(header->nack_data(full_size)));
	} else {
		set_answer(ProtocolAnswer(header->data(),header->data_len(full_size)));
	}

	return 1;
}