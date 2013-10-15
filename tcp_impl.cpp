#include "protocol.h"
#include "custom_combiners.h"

#include <iostream>
#include <iterator>
#include <algorithm>
#include <vector>
#include <string>

#define BOOST_ASIO_ENABLE_CANCELIO 

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

class Connector
{
	void async_connect(tcp::socket &socket, tcp::resolver::iterator i) {
		if(log_level) std::cerr << "async_connect " << std::endl;
		try {
			socket.async_connect(*i,boost::bind(&Connector::connect_callback,this,
				asio::placeholders::error,ref(socket),i));
		} catch(system::system_error &e) {
			std::cerr << "async_connect: " << e.what() << std::endl;
		}
	}

	void connect_callback(const system::error_code &error, tcp::socket &socket, tcp::resolver::iterator i) {
		if(log_level) std::cerr << "connect_callback " << error.message() << std::endl;
		if(error != asio::error::operation_aborted && ++i != tcp::resolver::iterator()) {
			async_connect(socket,i);
		} else {
			timeout.cancel();
			connect_promise.set_value(error);			
		}
	}

	void resolve_callback(const system::error_code &error, tcp::socket &socket, tcp::resolver::iterator i) {
		if(log_level) std::cerr << "resolve_callback " << error.message() << std::endl;
		if(error != asio::error::operation_aborted && i != tcp::resolver::iterator()) {
			async_connect(socket,i);
		} else {
			timeout.cancel();
			connect_promise.set_value(error);
		}
	}

	void timeout_callback(const system::error_code &error, tcp::socket &socket) {
		if(log_level) std::cerr << "timeout_callback " << error.message() << std::endl;
		if(error != asio::error::operation_aborted) {
			socket.close();
		} 
	}

public:
	Connector(asio::io_service &io_svc):resolver(io_svc),timeout(io_svc) {
		
	}

	system::error_code connect(tcp::socket &socket, const std::string &host, const std::string &service) {
		connect_promise = promise<system::error_code>();
		boost::unique_future<system::error_code> connect_future = connect_promise.get_future();

		tcp::resolver::query query(host, service);
		resolver.async_resolve(query,boost::bind(&Connector::resolve_callback,this,
			asio::placeholders::error,
			ref(socket),
			asio::placeholders::iterator));

		timeout.expires_from_now(posix_time::milliseconds(connect_timeout));
		timeout.async_wait(boost::bind(&Connector::timeout_callback,this,
			asio::placeholders::error,ref(socket)));

		return connect_future.get();
	}
private:
	tcp::resolver resolver;
	asio::deadline_timer timeout;
	promise<system::error_code> connect_promise;
};

class TcpImpl : public IOProvider
{
	asio::io_service io_svc;
	tcp::socket socket;
	asio::io_service::work work;
	asio::deadline_timer timeout;	
	
	thread io_thread;

	unsigned char read_buf[512];
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
			boost::bind(&TcpImpl::check_callback,this,ph::bytes_transferred,ph::error),
			boost::bind(&TcpImpl::read_callback,this,ph::bytes_transferred,ph::error));
	}

public:
	TcpImpl(const char *host_port, uint32_t baud, uint8_t)
		:socket(io_svc),work(io_svc),timeout(io_svc) {

		std::vector<std::string> tokens;
		split(tokens, host_port, is_any_of(":"));

		if(tokens.size() != 2) {
			throw_exception(system::system_error(boost::asio::error::invalid_argument));
		}

		io_thread = thread(boost::bind(&TcpImpl::io_service_thread,this));

		Connector connector(io_svc);
		system::error_code error = connector.connect(socket,tokens[0],tokens[1]);
		if (error) {
			if(log_level) std::cerr << "throw_exception" << std::endl;
			throw_exception(system::system_error(error));
		} else {
			initiate_read();
		}
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
	return boost::bind(disconnector,c);
}

void TcpImpl::send(void *data, size_t len,IOProvider::send_callback callback) 
{
	asio::async_write(this->socket,asio::buffer(data,len),
		boost::bind(&TcpImpl::write_callback,this,
		     callback,
		     asio::placeholders::bytes_transferred,
			 asio::placeholders::error));
}

long TcpImpl::set_timeout(size_t timeout, IOProvider::timeout_callback callback)
{
	if(log_level) std::cerr << "set_timeout" << std::endl;

	this->timeout.expires_from_now(posix_time::milliseconds(timeout));
	this->timeout.async_wait(boost::bind(&TcpImpl::wait_callback,this,callback,asio::placeholders::error));

	return 0;
}

long TcpImpl::cancel_timeout()
{
	if(log_level) std::cerr << "cancel_timeout" << std::endl;

	this->timeout.cancel();

	return 0;
}

IOProvider* create_tcp_impl(const char* path,uint32_t baud,uint8_t parity)
{
	return new TcpImpl(path,baud,parity);
}