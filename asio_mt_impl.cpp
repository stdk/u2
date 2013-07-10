#include "protocol.h"

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

using namespace std;
using namespace boost;

#define PACKET_RECEIVE_TIMEOUT 2000

static const int log_level = 0;

//define required functions from other modules
void debug_data(const char* header,void* data,size_t len);

static uint8_t* find_packet_begin(uint8_t * data,size_t len)
{
	uint8_t *begin = data;
	while(len-- && *begin != FBGN) begin++;
	if(*begin != FBGN) return 0;
	return begin;
}

class AsioMTImpl : public IReaderImpl
{
	asio::io_service io_svc;
	asio::serial_port serial;
	asio::deadline_timer timeout;
	
	unsigned char read_buf[512];
	unsigned char packet_buf[512];
	unsigned char write_buf[512];

	weak_ptr< promise<size_t> > result_promise;
	
	promise<bool> stop_promise;
	unique_future<bool> stopped;

	// read_callback - this callback sets data_available flag and cancels timeout callback to
	// prevent its calling. 
	// After that, since there is no more callbacks pending, 
	// io_svc.run() completes, but(!) we prevent this by initiate_read, 
	// that starts another cycle of packet reading
	void read_callback(size_t bytes_transferred,const system::error_code& error)
	{
		initiate_read();
		
		if (error || !bytes_transferred)
		{
			cerr << "read callback error:" << error << ": " << error.message() << endl;
			return;
		}

		//timeout can now be canceled.
		timeout.cancel(); 
	}

	// check_callback - called every time new portion of data comes from serial port and 
	// decides whether received data can be called packet - if it can, this callback returns
	// true that leads to read_callback being called
	bool check_callback(size_t bytes_transferred,const system::error_code& error)
	{
		if(error) {
			//cerr << "check callback error = " << error << endl;
			return true;
		}
	
		if(log_level) debug_data("Check callback bytes",read_buf,bytes_transferred);
	
		// data from serial port should begin from correct byte
		uint8_t* packet_begin = find_packet_begin(read_buf,bytes_transferred);
		if(!packet_begin) return false;
	
		size_t current_length = bytes_transferred - (packet_begin - read_buf);
		if(log_level) debug_data("Selected",packet_begin,current_length);

		// there should be at least sizeof(PacketHeader) bytes to treat data as packet
		if(current_length <= sizeof(PacketHeader)) return false;

		size_t packet_len = unbytestaff(packet_buf,sizeof(packet_buf),packet_begin,current_length);
		if(log_level) debug_data("Unbytestaffed",packet_buf,packet_len);

		// unbytestaffed packet length should be at least as long as its header specifies
		if(packet_len < ((PacketHeader*)packet_buf)->full_size()) return false;

		if(shared_ptr< promise<size_t> > p = result_promise.lock()) {
			p->set_value(packet_len);
		}

		return true; // will cause read_callback to fire without error
	}

	// wait_callback - fires when maximum packet waiting time expired. If its called, that means not 
	// enough data came from the serial port to be recognized as a complete packet - so, serial port
	// operations are canceled and io_svc run completes without data_available flag set.
	// Serial callbacks cancel leads to calling read_callback and wait_callback with error flag.
	// Initiated in AsioImpl::tranceive
	void wait_callback(const system::error_code& error)
	{
		if (error) {
			//cerr << "wait_callback error:" << error << ": " << error.message() << endl;
			return;   // Data was read and this timeout was canceled
		}
		if(log_level) cerr << "wait_callback"<< endl;
				
		//serial.cancel();

		if(shared_ptr< promise<size_t> > p = result_promise.lock()) {
			p->set_value(0);
		}
	}

	unsigned char *read_buf_current;

	void read_callback2(size_t bytes_transferred,const system::error_code& error)
	{
		if (error || !bytes_transferred)
		{
			cerr << "read callback error:" << error << ": " << error.message() << endl;
			read_buf_current = read_buf;
			return initiate_read();
		}

		read_buf_current += bytes_transferred;

		cerr << read_buf_current - read_buf << endl;
		if(read_buf_current - read_buf < 5) {
			debug_data("x",read_buf,read_buf_current - read_buf);
		}

		if(log_level) debug_data("Check callback bytes",read_buf,read_buf_current - read_buf);
	
		// data from serial port should begin from correct byte
		uint8_t* packet_begin = find_packet_begin(read_buf,bytes_transferred);
		if(!packet_begin) {
			read_buf_current = read_buf;
			return initiate_read();
		}
	
		size_t current_length = read_buf_current - packet_begin;
		
		if(log_level) debug_data("Selected",packet_begin,current_length);

		// there should be at least sizeof(PacketHeader) bytes to treat data as packet
		if(current_length <= sizeof(PacketHeader)) return initiate_read();

		size_t packet_len = unbytestaff(packet_buf,sizeof(packet_buf),packet_begin,current_length);
		if(log_level) debug_data("Unbytestaffed",packet_buf,packet_len);

		// unbytestaffed packet length should be at least as long as its header specifies
		if(packet_len < ((PacketHeader*)packet_buf)->full_size()) return initiate_read();

		//debug_data("in",read_buf,bytes_transferred);

		if(shared_ptr< promise<size_t> > p = result_promise.lock()) {
			p->set_value(packet_len);
		}	

		//timeout can now be canceled.
		timeout.cancel(); 

		read_buf_current = read_buf;
		initiate_read();
	}

	// Begin new iteration of packet read by executing async_read 
	// that keeps io_service running to wait for new packet
	inline void initiate_read() {
		namespace ph = boost::asio::placeholders;
		 
		

		/*asio::async_read(this->serial,asio::buffer(read_buf_current,sizeof(read_buf) - (read_buf_current - read_buf)),
			bind(&AsioMTImpl::read_callback2,this,ph::bytes_transferred,ph::error));*/

		asio::async_read(this->serial,asio::buffer(read_buf),
			bind(&AsioMTImpl::check_callback,this,ph::bytes_transferred,ph::error),
			bind(&AsioMTImpl::read_callback,this,ph::bytes_transferred,ph::error));
	}

public:
	AsioMTImpl(const char* path,uint32_t baud):serial(io_svc),timeout(io_svc),stopped(stop_promise.get_future()) {
		
		serial.open( path );

		const asio::serial_port_base::parity parity_opt(asio::serial_port_base::parity::none);
		const asio::serial_port_base::stop_bits stop_bits_opt(asio::serial_port_base::stop_bits::one);

		serial.set_option(asio::serial_port_base::baud_rate(baud));
		serial.set_option(asio::serial_port_base::character_size(8)); 
		serial.set_option(parity_opt);
		serial.set_option(stop_bits_opt);
		serial.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none)); 

		read_buf_current = read_buf;
		initiate_read();

		thread t(bind(&AsioMTImpl::io_service_thread,this));
		t.detach();
	}

	void io_service_thread() {

		while(true) {
			try {
				io_svc.run();
				serial.close(); //serial port should be closed from the same thread as io service
				stop_promise.set_value(true);
				return;
			} catch(boost::system::system_error& e) {
				cerr << "io_service_thread:" << e.what() << endl;
				//promise.set_exception(current_exception());
				io_svc.reset();
			}
		}
	}

	virtual ~AsioMTImpl() {
		io_svc.stop();
		stopped.get();
	}

	long transceive(void* data,size_t len,void* packet,size_t packet_len);
};

long AsioMTImpl::transceive(void* data,size_t len,void* packet,size_t packet_len)
{
	size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

	if(log_level) debug_data("send_command[send]",write_buf,write_buf_len);

	PacketHeader *header = (PacketHeader*)packet;
	try {
		shared_ptr< promise<size_t> > new_promise( new promise<size_t>() );
		unique_future<size_t> response_length_future = new_promise->get_future();

		result_promise = new_promise;
				
		asio::write(this->serial,asio::buffer(write_buf,write_buf_len));
		
		this->timeout.expires_from_now(posix_time::milliseconds(PACKET_RECEIVE_TIMEOUT));
		this->timeout.async_wait(bind(&AsioMTImpl::wait_callback, this, boost::asio::placeholders::error));

		size_t response_length = response_length_future.get();
		if(!response_length) return NO_ANSWER;
		if(response_length > packet_len) return ANSWER_TOO_LONG;
		memcpy(packet,packet_buf,min(response_length,packet_len));

		return 0;	

	} catch(boost::system::system_error& e) {
		cerr << e.what() << endl;
		return IO_ERROR;
	}

}

IReaderImpl* create_asio_mt_impl(const char* path,uint32_t baud)
{
	return new AsioMTImpl(path,baud);
}