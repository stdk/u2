#ifndef TERMINAL_PROTOCOL
#define TERMINAL_PROTOCOL

#include "protocol.h"

#define FSSTR            '>'
#define FMSTR            '<'
#define FEND             ';'

#define FMAS             '?'
#define FSLV             '!'
#define FNAK             '-'
#define FACK             '+'

#define FMID             '/'

#define BRDCAST          0xff

size_t terminal_bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len);

class TerminalUnbytestaffer
{
	uint8_t buffer[1024];
	uint8_t *sink;
	bool wait_for_start;
	bool _completed;
	bool escape;
public:
	TerminalUnbytestaffer();

    void reset();

	size_t feed(void *data, size_t len);

	template<typename T>
	inline T get() {
		return (T)buffer;
	}

	inline size_t size() const {
		return sink - buffer;
	}

	inline bool completed() {
		return _completed;
	}
};

class TerminalProtocol : public Protocol
{
	IOProvider *provider;
	function<void ()> disconnect;

	TerminalUnbytestaffer filter;

	uint8_t type;
	uint8_t addr;
	uint8_t code;
	uint8_t write_buf[1024];

	size_t timeout;
	void timeout_callback();

	// Receives block of data that should be parsed according to protocol tules.
	// Return values: 
	// -1 -> data cannot be parsed as a protocol packet;
	//  0 -> there is not enough data to make decision about packet;
	//  1 -> packet has been successfully formed from data given so far;
	long feed(void *data, size_t len);

	long write_callback(size_t bytes_transferred, size_t bytes_sent_to_transfer,const system::error_code &error);

	void set_answer(ProtocolAnswer answer);
public:
	TerminalProtocol(IOProvider *_provider);
	virtual ~TerminalProtocol();

	inline void set_timeout(size_t _timeout) {
		timeout = _timeout;
	}

	inline void set_type(uint8_t _type) {
		type = _type;
	}

	virtual long send(uint8_t _addr, uint8_t _code, void *data, size_t len);	
};


#endif