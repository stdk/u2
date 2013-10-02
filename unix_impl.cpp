#include "protocol.h"

#include <iostream>
#include <iterator>
#include <algorithm>
#include <vector>
#include <string>

#include <boost/bind.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio.hpp> 
#include <boost/lambda/lambda.hpp>
#include <boost/signals2.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/throw_exception.hpp>

using namespace boost;

using boost::asio::local::stream_protocol;

static const int log_level = getenv("DEBUG_UNIX_IMPL") != 0;
static const size_t connect_timeout = 3000;

class UnixImpl : public IOProvider
{
	asio::io_service io_svc;
	stream_protocol::socket socket;
	asio::io_service::work work;
	asio::deadline_timer timeout;	
	
	thread io_thread;

	unsigned char read_buf[512];
	
	signals2::signal<long (void *data, size_t len)> data_received;

	void read_callback(size_t bytes_transferred,const system::error_code& error)
	{
		if (error || !bytes_transferred)
		{
			if(log_level) std::cerr << "read callback error:" << error << ": " << error.message() << std::endl;
			return;
		}

		if(log_level) debug_data("read_callback",read_buf,bytes_transferred);
	    data_received(read_buf,bytes_transferred);
		initiate_read();
	}

	// Any byte is precious to us.
	bool check_callback(size_t bytes_transferred,const system::error_code& error)
	{
		if(error) return true;
	
		return bytes_transferred > 0;
	}

	void write_callback(IOProvider::send_callback callback,size_t bytes_transferred,const system::error_code& error)
	{
		callback(bytes_transferred,error);
	}

	void wait_callback(function<void ()> callback, const system::error_code& error)
	{
		if (error) return;   // Data has been read and this timeout was canceled
		
		callback();
	}

	// Begin new iteration of packet read by executing async_read 
	// that keeps io_service running to wait for new packet.
	//1. check_callback - called every time new portion of data comes from socket and 
	//   decide whether received should be passed to read_callback.
	//2. read_callback - this callback signals about receiving data and decides what to do next
	//   by its return value
	inline void initiate_read() {
		if(log_level) std::cerr << "initiate_read" << std::endl;

		namespace ph = boost::asio::placeholders;
		 
		asio::async_read(this->socket,asio::buffer(read_buf),
			bind(&UnixImpl::check_callback,this,ph::bytes_transferred,ph::error),
			bind(&UnixImpl::read_callback,this,ph::bytes_transferred,ph::error));
	}

public:
	UnixImpl(const char *path, uint32_t, uint8_t)
		:socket(io_svc),work(io_svc),timeout(io_svc) {
		
		socket.connect(path);
		
		initiate_read();

		io_thread = thread(bind(&UnixImpl::io_service_thread,this));
	}

	void io_service_thread() {
		if(log_level) std::cerr << "starting io_svc" << std::endl;

		system::error_code e;
		io_svc.run(e);
		if(e) std::cerr << "io_svc: " << e.message() << std::endl;
		
		if(log_level) std::cerr << "io_svc[stopped]" << std::endl;
		//socket.shutdown(asio::socket_base::shutdown_both);
		
		socket.close(e); //socket closed from the same thread as io service
		if(e) std::cerr << "socket.close() : " << e.message() << std::endl;
	}

	virtual ~UnixImpl() {
		io_svc.stop();
		io_thread.join();
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

function<void ()> UnixImpl::listen(IOProvider::listen_callback callback)
{
	if(log_level) std::cerr << "listen" << std::endl;

	signals2::connection c = data_received.connect(callback);
	return bind(disconnector,c);
}

void UnixImpl::send(void *data, size_t len,IOProvider::send_callback callback) 
{
	asio::async_write(this->socket,asio::buffer(data,len),
		bind(&UnixImpl::write_callback,this,
		     callback,
		     asio::placeholders::bytes_transferred,
			 asio::placeholders::error));
}

long UnixImpl::set_timeout(size_t timeout, IOProvider::timeout_callback callback)
{
	if(log_level) std::cerr << "set_timeout" << std::endl;

	this->timeout.expires_from_now(posix_time::milliseconds(timeout));
	this->timeout.async_wait(bind(&UnixImpl::wait_callback,this,callback,asio::placeholders::error));

	return 0;
}

long UnixImpl::cancel_timeout()
{
	if(log_level) std::cerr << "cancel_timeout" << std::endl;

	this->timeout.cancel();

	return 0;
}

IOProvider* create_unix_impl(const char* path,uint32_t baud,uint8_t parity)
{
	return new UnixImpl(path,baud,parity);
}