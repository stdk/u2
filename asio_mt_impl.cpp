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

#ifdef WIN32
#include <windows.h>
#endif

using namespace boost;

static const int log_level = 0;

class AsioMTImpl : public IOProvider
{
	asio::io_service io_svc;
	asio::serial_port serial;
	asio::io_service::work work;	
	asio::deadline_timer timeout;	
	
	thread io_thread;

	unsigned char read_buf[512];
	size_t read_size;
	

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

		long packet_found = data_received(read_buf,bytes_transferred);
		if(!packet_found) {
			initiate_read();
		}
	}

	// Any byte that came from serial port is precious.
	bool check_callback(size_t bytes_transferred,const system::error_code& error)
	{
		if(error) return true;
	
		return bytes_transferred > 0;
	}

	void write_callback(IOProvider::send_callback callback,size_t bytes_transferred,const system::error_code& error)
	{
		if(callback(bytes_transferred,error) == 0) {
			initiate_read();
		}
	}

	static void canceller(asio::serial_port &s) {
		s.cancel();
	}

	void wait_callback(function<void ()> callback, const system::error_code& error)
	{
		if (error) return;   // Data has been read and this timeout was canceled
		
		callback();

		// to make io_svc.run return we should cancel all pending operations on serial port    
		
#ifdef WIN32
		// workaround: serial.cancel() terminates the runtime on Windows but CancelIo works properly
		CancelIo(serial.native_handle());
#else
		serial.cancel();
#endif	
	}

	// Begin new iteration of packet read by executing async_read 
	// that keeps io_service running to wait for new packet.
	//1. check_callback - called every time new portion of data comes from serial port and 
	//   decide whether received should be passed to read_callback.
	//2. read_callback - this callback signals about receiving data and decides what to do next.
	//   according to its return value
	inline void initiate_read() {
		namespace ph = boost::asio::placeholders;
		 
		asio::async_read(this->serial,asio::buffer(read_buf,read_size),
			bind(&AsioMTImpl::check_callback,this,ph::bytes_transferred,ph::error),
			bind(&AsioMTImpl::read_callback,this,ph::bytes_transferred,ph::error));
	}

public:
	AsioMTImpl(const char *path,uint32_t baud):serial(io_svc),work(io_svc),timeout(io_svc),read_size(1) {
		
		serial.open( path );

		const asio::serial_port_base::parity parity_opt(asio::serial_port_base::parity::none);
		const asio::serial_port_base::stop_bits stop_bits_opt(asio::serial_port_base::stop_bits::one);

		serial.set_option(asio::serial_port_base::baud_rate(baud));
		serial.set_option(asio::serial_port_base::character_size(8)); 
		serial.set_option(parity_opt);
		serial.set_option(stop_bits_opt);
		serial.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none)); 

		io_thread = thread(bind(&AsioMTImpl::io_service_thread,this));
	}

	void io_service_thread() {
		io_svc.run();
		std::cerr << "io_svc stopped" << std::endl;
		serial.close(); //serial port should be closed from the same thread as io service
	}

	virtual ~AsioMTImpl() {
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

function<void ()> AsioMTImpl::listen(IOProvider::listen_callback callback)
{
	if(log_level) std::cerr << "listen" << std::endl;

	signals2::connection c = data_received.connect(callback);
	return bind(disconnector,c);
}

void AsioMTImpl::send(void *data, size_t len,IOProvider::send_callback callback) 
{
	asio::async_write(this->serial,asio::buffer(data,len),
		bind(&AsioMTImpl::write_callback,this,
		     callback,
		     asio::placeholders::bytes_transferred,
			 asio::placeholders::error));
}

long AsioMTImpl::set_timeout(size_t timeout, IOProvider::timeout_callback callback)
{
	if(log_level) std::cerr << "set_timeout" << std::endl;

	this->timeout.expires_from_now(posix_time::milliseconds(timeout));
	this->timeout.async_wait(bind(&AsioMTImpl::wait_callback,this,callback,asio::placeholders::error));

	return 0;
}

long AsioMTImpl::cancel_timeout()
{
	if(log_level) std::cerr << "cancel_timeout" << std::endl;

	this->timeout.cancel();

	return 0;
}

IOProvider* create_asio_mt_impl(const char* path,uint32_t baud)
{
	return new AsioMTImpl(path,baud);
}