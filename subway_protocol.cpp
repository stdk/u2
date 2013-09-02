#include "subway_protocol.h"

#include <boost/bind.hpp>

using namespace boost;

static const size_t TIMEOUT = 1500;
static const int log_level = 0;

Unbytestaffer::Unbytestaffer() {
	reset();
}

size_t Unbytestaffer::feed(void *data, size_t len) {
	if(!data || !len) return 0;

	uint8_t *src = (uint8_t*)data;
	uint8_t *src_end = src + len;
	uint8_t *dst = sink;
	uint8_t *dst_end = buffer + sizeof(buffer);

	if(wait_for_fbgn) {
		while(src != src_end && *src != FBGN && src++);
		if(src != src_end) wait_for_fbgn = false;
	}

	while( src != src_end && dst != dst_end ) {
		uint8_t c = *src++;
		if(escape) {
			*dst++ = c == TFBGN ? FBGN : FESC;
			if(c != TFBGN && c != TFESC && dst != dst_end) *dst++ = c;
			escape = false;
		} else {
			if(c == FESC) escape = true;
			else *dst++ = c;
		}
	}
	size_t bytes_parsed = dst - sink;
	sink = dst;
	return bytes_parsed;
}

void Unbytestaffer::reset() {
	sink = buffer;
	wait_for_fbgn = true;
	escape = false;
}


SubwayProtocol::SubwayProtocol(IOProvider *_provider):provider(_provider)
{
	disconnect = provider->listen(bind(&SubwayProtocol::feed,this,_1,_2));
}

SubwayProtocol::~SubwayProtocol()
{
	if(!disconnect.empty()) disconnect();
}

// This method will be used as callback in IOProvider->set_timeout.
// Fires when maximum packet waiting time expired. If its called, that means not 
// enough data came from the serial port to be recognized as a complete packet by read callbacks.
void SubwayProtocol::timeout()
{
	if(log_level) std::cerr << "SubwayProtocol::timeout" << std::endl;
	set_answer(ProtocolAnswer(NO_ANSWER));
}

long SubwayProtocol::write_callback(size_t bytes_sent_to_transfer, size_t bytes_transferred,const system::error_code &error)
{
	if (error)
	{
		std::cerr << "write_callback error:" << error << ": " << error.message() << std::endl;
		set_answer(ProtocolAnswer(IO_ERROR));
		return -1;
	}

	if(log_level) std::cerr << "write_callback: " << bytes_transferred << "/" << bytes_sent_to_transfer << std::endl;

	provider->set_timeout(TIMEOUT,bind(&SubwayProtocol::timeout,this));

	return 0;
}

void SubwayProtocol::send(void *data, size_t len) {
	size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

	if(log_level) debug_data("send",write_buf,write_buf_len);

	provider->send(write_buf,write_buf_len,
		bind(&SubwayProtocol::write_callback,this,write_buf_len,_1,_2));
}

void SubwayProtocol::set_answer(ProtocolAnswer answer)
{
	if(!disconnect.empty()) {
		disconnect();
		disconnect.clear();
	}
	provider->cancel_timeout();
	Protocol::set_answer(answer);
}

long SubwayProtocol::feed(void *data, size_t len) {
	if(log_level) debug_data("feed",data,len);

	if(!data || !len) return 0;

	filter.feed(data,len);

	if(filter.size() < sizeof(PacketHeader)) return 0;

	size_t full_packet_size = filter.get<PacketHeader*>()->full_size();
	if(filter.size() < full_packet_size) return 0;

	set_answer(ProtocolAnswer(filter.get<void*>(), full_packet_size));
		
	return 1;
}