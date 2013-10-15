#include "protocol.h"
#include "custom_combiners.h"

#include <iostream>
#include <iterator>
#include <algorithm>
#include <vector>
#include <string>

#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp> 
#include <boost/asio/serial_port.hpp> 
#include <boost/lambda/lambda.hpp>
#include <boost/signals2.hpp>

using namespace boost;

static const int log_level = 0;

//define required functions from other modules
void debug_data(const char* header,void* data,size_t len);

class AsioImpl : public IOProvider
{
	asio::io_service io_svc;
	asio::serial_port serial;
	asio::deadline_timer timeout;

	unsigned char read_buf[512];
	size_t read_size;

	// Using maximum combiner assures that if more than one listener will be active
	// on this signal, we should always get maximum of their return values.
	// As a result, we will correctly stop io_service when at least one listener got what it wanted.
	signals2::signal<long (void *data, size_t len),combiner::maximum<long> > data_received;

	void read_callback(size_t bytes_transferred,const system::error_code &error)
	{
		if (error || !bytes_transferred)
		{
			std::cerr << "read callback error:" << error << ": " << error.message() << std::endl;
			return;
		}

		long packet_found = data_received(read_buf,bytes_transferred);
		if(!packet_found) {
			initiate_read();
		}
	}

	// Any byte that came from serial port is precious.
	bool check_callback(size_t bytes_transferred,const system::error_code &error)
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

	void wait_callback(function<void ()> callback, const system::error_code& error)
	{
		if (error) return;   // Data has been read and this timeout was canceled
		
		callback();

		// to make io_svc.run return we should cancel all pending operations on serial port    
		serial.cancel(); 
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
			boost::bind(&AsioImpl::check_callback,this,ph::bytes_transferred,ph::error),
			boost::bind(&AsioImpl::read_callback,this,ph::bytes_transferred,ph::error));
	}
public:
	AsioImpl(const char* path,uint32_t baud,uint8_t parity)
		:serial(io_svc),timeout(io_svc),read_size(1) {
		
		serial.open( path );

		const asio::serial_port_base::parity parity_opt((asio::serial_port_base::parity::type)parity);
		const asio::serial_port_base::stop_bits stop_bits_opt(asio::serial_port_base::stop_bits::one);

		serial.set_option(asio::serial_port_base::baud_rate(baud));
		serial.set_option(asio::serial_port_base::character_size(8)); 
		serial.set_option(parity_opt);
		serial.set_option(stop_bits_opt);
		serial.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none)); 
	}

	virtual ~AsioImpl() {
		serial.close();
	}

	virtual function<void ()> listen(IOProvider::listen_callback callback);
	virtual void send(void *data, size_t len, IOProvider::send_callback callback);
	virtual long set_timeout(size_t timeout, IOProvider::timeout_callback callback);
	virtual long cancel_timeout();
};

static void disconnector(signals2::connection c)
{
	c.disconnect();
}

function<void ()> AsioImpl::listen(IOProvider::listen_callback callback)
{
	signals2::connection c = data_received.connect(callback);
	return boost::bind(disconnector,c);
}

void AsioImpl::send(void *data, size_t len, IOProvider::send_callback callback)
{
	asio::async_write(this->serial,asio::buffer(data,len),
		boost::bind(&AsioImpl::write_callback,this,
		     callback,
		     asio::placeholders::bytes_transferred,
			 asio::placeholders::error));
	this->io_svc.run();
	this->io_svc.reset();
}

long AsioImpl::set_timeout(size_t timeout, IOProvider::timeout_callback callback)
{
	this->timeout.expires_from_now(posix_time::milliseconds(timeout));
	this->timeout.async_wait(boost::bind(&AsioImpl::wait_callback,this,callback,asio::placeholders::error));

	return 0;
}

long AsioImpl::cancel_timeout()
{
	this->timeout.cancel();

	return 0;
}

IOProvider* create_asio_impl(const char* path,uint32_t baud,uint8_t parity)
{
	return new AsioImpl(path,baud,parity);
}