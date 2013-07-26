#ifndef SUBWAY_PROTOCOL
#define SUBWAY_PROTOCOL

#include "protocol.h"

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

	virtual void send(void *data, size_t len);	
};

#endif