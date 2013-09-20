#include "protocol.h"
#include "custom_combiners.h"

#include <iostream>
#include <iterator>
#include <algorithm>
#include <vector>
#include <string>

#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio.hpp> 
#include <boost/asio/serial_port.hpp> 
#include <boost/lambda/lambda.hpp>
#include <boost/signals2.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/throw_exception.hpp>

#include <time.h>
#include <stdlib.h>

#include <cp210x.h>

using namespace boost;

static const int log_level = getenv("DEBUG_CP210X_IMPL") != 0;

class Timeout
{
	int active;
	uint64_t final;
	function<void ()> callback;
	
	// measured in miliseconds
	static uint64_t get_tick_count() {
		struct timespec ts;
		if(clock_gettime(CLOCK_MONOTONIC,&ts) != 0) {
			perror("clock_gettime");
		}
		
		uint64_t v = ts.tv_sec * 1000 + ts.tv_nsec / 1e6;
		
		if(log_level) fprintf(stderr,"get_tick_count() = %lli\n",v);				
		
		return v;
	}
public:
	Timeout():active(false) {

	}

	void set(uint64_t timeout,function<void ()> timeout_callback) {
		active = true;
		callback = timeout_callback;
		final = get_tick_count() + timeout;
	}
	
	void cancel() {
		active = false;
	}

	bool check() {
		if(active && !time_left()) {
			callback();
			active = false;
			return true;
		}
		return false;
	}

	uint64_t time_left() const {
		return final > get_tick_count() ? final - get_tick_count() : 0;
	}
};

class CP210XImpl : public IOProvider
{
	usb::context ctx;
	cp210x device;

	Timeout timeout;

	signals2::signal<long (void *data, size_t len),combiner::maximum<long>> data_received;
public:
	CP210XImpl(const char *path,uint32_t baud,uint8_t parity):ctx(3),device(ctx) {
		if(!device) {
			throw_exception(system::system_error(system::error_code(ENODEV,
			                                     system::system_category())));
		}
		
		device.set_baud(baud);
			
		cp210x::parity_t parity_opt;
		switch(parity) {
			case PARITY::ODD:
				parity_opt = cp210x::parity_odd;
				break;
			case PARITY::EVENT:
				parity_opt = cp210x::parity_even;
				break;
			default:
				parity_opt = cp210x::parity_none;
				break;
		}
		device.set_ctl(cp210x::data8 | parity_opt | cp210x::stop1);
	}	

	virtual ~CP210XImpl() {
		if(log_level) std::cerr << "~CP210XImpl" << std::endl;
	}
	
	virtual function<void ()> listen(IOProvider::listen_callback callback);
	virtual void send(void *data, size_t len, IOProvider::send_callback callback);
	virtual long set_timeout(size_t timeout, IOProvider::timeout_callback callback);
	virtual long cancel_timeout();
};

static void disconnector(signals2::connection c)
{
	if(log_level) std::cerr << "disconnect" << std::endl;

	c.disconnect();
}

function<void ()> CP210XImpl::listen(IOProvider::listen_callback callback)
{
	if(log_level) std::cerr << "listen" << std::endl;

	auto recv_handler = [=](int status, void *data, size_t len) {
		if(!this->timeout.check()) {
			if(!callback(data,len)) {
				this->device.recv_async();
			}
		}
	};

	auto c = device.data_received.connect(recv_handler);
	
	return bind(disconnector,c);
}

void CP210XImpl::send(void *data, size_t len,IOProvider::send_callback callback) 
{
	int ret = device.send_async(data,len,[=](int status,size_t len) {
		if(log_level) std::cerr << "CP210XImpl::data_sent" << std::endl;
		
		if(!callback(len,system::error_code(status ? EIO : 0,system::system_category()))) {
			this->device.recv_async();
		}
	});
	
	if(ret) {
		callback(0,system::error_code(errno,system::system_category()));
	}	
	
	/*
	int ret = device.send(data,len);
	if(callback(ret > 0 ? ret : 0,) == 0) {
		int bytes_read = 0;
		do {
			if(timeout.check()) break;
			bytes_read = device.recv(read_buf,sizeof(read_buf),timeout.time_left());
			if(bytes_read == -1) continue;
		} while(!data_received(read_buf,bytes_read));
	}*/
}

long CP210XImpl::set_timeout(size_t timeout, IOProvider::timeout_callback callback)
{
	if(log_level) std::cerr << "set_timeout" << std::endl;

	this->timeout.set(timeout,callback);

	return 0;
}

long CP210XImpl::cancel_timeout()
{
	if(log_level) std::cerr << "cancel_timeout" << std::endl;

	this->timeout.cancel();

	return 0;
}

IOProvider* create_cp210x_impl(const char* path,uint32_t baud,uint8_t parity)
{
	return new CP210XImpl(path,baud,parity);
}