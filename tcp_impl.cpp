#include "protocol.h"
#include "custom_combiners.h"

#include <iostream>
#include <iterator>
#include <algorithm>
#include <vector>
#include <string>

//#define BOOST_ASIO_ENABLE_CANCELIO 

#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio.hpp> 
#include <boost/lambda/lambda.hpp>
#include <boost/signals2.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/throw_exception.hpp>

using namespace boost;

using boost::asio::ip::tcp;

static const int log_level = 0;
static const size_t connect_timeout = 3000;

/*
void set_socket_timeouts(boost::asio::ip::tcp::socket& socket,size_t timeout_ms)
{
#ifdef WIN32
    int32_t timeout = timeout_ms;
    setsockopt(socket.native(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(socket.native(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000; 
    tv.tv_usec = (timeout_ms % 1000) * 1000;         
    setsockopt(socket.native(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socket.native(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}*/



class TcpImpl : public IOProvider
{
	asio::io_service io_svc;
	tcp::resolver resolver;
	tcp::socket socket;
	//shared_ptr<asio::io_service::work> work;
	asio::io_service::work work;
	asio::deadline_timer timeout;	
	
	thread io_thread;

	unsigned char read_buf[64];
	size_t read_size;
	
	boost::promise<system::error_code> connect_promise;

	// Using maximum combiner assures that if more than one listener will be active
	// on this signal, we should always get maximum of their return values.
	// As a result, we will correctly stop io_service when at least one listener got what it wanted.
	signals2::signal<long (void *data, size_t len),combiner::maximum<long> > data_received;

	// read_callback - passed information about bytes in read_buf to handle_by_protocol method.	
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

		/*long packet_found = data_received(read_buf,bytes_transferred);
		if(!packet_found) {
			initiate_read();
		}*/
	}

	// Any byte that came from serial port is precious.
	bool check_callback(size_t bytes_transferred,const system::error_code& error)
	{
		if(error) return true;
	
		return bytes_transferred > 0;
	}

	void write_callback(IOProvider::send_callback callback,size_t bytes_transferred,const system::error_code& error)
	{
		callback(bytes_transferred,error);
		
		/*if(callback(bytes_transferred,error) == 0) {
			initiate_read();
		}*/
	}

	void wait_callback(function<void ()> callback, const system::error_code& error)
	{
		if (error) return;   // Data has been read and this timeout was canceled
		
		callback();

		if(log_level) fprintf(stderr,"socket.cancel() begin\n");

		//socket.cancel();

		if(log_level) fprintf(stderr,"socket.cancel() completed\n");
	}

	// Begin new iteration of packet read by executing async_read 
	// that keeps io_service running to wait for new packet.
	//1. check_callback - called every time new portion of data comes from socket and 
	//   decide whether received should be passed to read_callback.
	//2. read_callback - this callback signals about receiving data and decides what to do next.
	//   according to its return value
	inline void initiate_read() {
		if(log_level) std::cerr << "initiate_read" << std::endl;

		namespace ph = boost::asio::placeholders;
		 
		asio::async_read(this->socket,asio::buffer(read_buf),
			bind(&TcpImpl::check_callback,this,ph::bytes_transferred,ph::error),
			bind(&TcpImpl::read_callback,this,ph::bytes_transferred,ph::error));
	}

	void connect_callback(const system::error_code &error, tcp::resolver::iterator i) {
		if(error && ++i != tcp::resolver::iterator()) {
			async_connect(i);
		} else {
			timeout.cancel();
			connect_promise.set_value(error);			
		}
	}

	void async_connect(tcp::resolver::iterator i) {
		if(log_level) std::cerr << "async_connect " << std::endl;
		socket.async_connect(*i,bind(&TcpImpl::connect_callback,this,asio::placeholders::error,i));
	}

	void resolve_callback(const system::error_code &error, tcp::resolver::iterator i) {
		if(error) {
			timeout.cancel();
			connect_promise.set_value(error);
		} else {
			async_connect(i);
		}
	}

	void connect_timeout_callback(const system::error_code &error) {
		if(error != asio::error::operation_aborted) {
			connect_promise.set_value(error);
		}
	}

public:
	TcpImpl(const char *host_port, uint32_t baud):resolver(io_svc),socket(io_svc),
		//work(new asio::io_service::work(io_svc)),
		work(io_svc),
		timeout(io_svc) {

		std::vector<std::string> tokens;
		split(tokens, host_port, is_any_of(":"));

		if(tokens.size() != 2) {
			throw_exception(system::system_error(boost::asio::error::invalid_argument));
		}
/*
		tcp::resolver resolver(io_svc);
		tcp::resolver::query query( tokens[0], tokens[1]);
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;

		boost::system::error_code error = boost::asio::error::host_not_found;
		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}*/

		tcp::resolver::query query( tokens[0], tokens[1]);
		resolver.async_resolve(query,bind(&TcpImpl::resolve_callback,this,
			asio::placeholders::error,asio::placeholders::iterator));

		timeout.expires_from_now(posix_time::milliseconds(connect_timeout));
		timeout.async_wait(bind(&TcpImpl::connect_timeout_callback,this,asio::placeholders::error));

		boost::unique_future<system::error_code> connect_future = connect_promise.get_future();

		io_thread = thread(bind(&TcpImpl::io_service_thread,this));

		system::error_code error = connect_future.get();
		if (error) {
			if(log_level) std::cerr << "throw_exception" << std::endl;
			throw_exception(system::system_error(error));
		} else {
			initiate_read();
		}
	}

	void io_service_thread() {
		if(log_level) std::cerr << "starting io_svc" << std::endl;
		io_svc.run();
		if(log_level) std::cerr << "io_svc stopped" << std::endl;
		//socket.shutdown(asio::socket_base::shutdown_both);
		
		system::error_code e;
		socket.close(e); //socket closed from the same thread as io service
	}

	virtual ~TcpImpl() {
		//work.reset();
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

function<void ()> TcpImpl::listen(IOProvider::listen_callback callback)
{
	if(log_level) std::cerr << "listen" << std::endl;

	signals2::connection c = data_received.connect(callback);
	return bind(disconnector,c);
}

void TcpImpl::send(void *data, size_t len,IOProvider::send_callback callback) 
{
	asio::async_write(this->socket,asio::buffer(data,len),
		bind(&TcpImpl::write_callback,this,
		     callback,
		     asio::placeholders::bytes_transferred,
			 asio::placeholders::error));
}

long TcpImpl::set_timeout(size_t timeout, IOProvider::timeout_callback callback)
{
	if(log_level) std::cerr << "set_timeout" << std::endl;

	this->timeout.expires_from_now(posix_time::milliseconds(timeout));
	this->timeout.async_wait(bind(&TcpImpl::wait_callback,this,callback,asio::placeholders::error));

	return 0;
}

long TcpImpl::cancel_timeout()
{
	if(log_level) std::cerr << "cancel_timeout" << std::endl;

	this->timeout.cancel();

	return 0;
}

IOProvider* create_tcp_impl(const char* path,uint32_t baud)
{
	return new TcpImpl(path,baud);
}