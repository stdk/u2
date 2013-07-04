#include "protocol.h"

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

using namespace std;
using namespace boost;

#define PACKET_RECEIVE_TIMEOUT 3000

static const int log_level = 0;

//define required functions from other modules
void debug_data(const char* header,void* data,size_t len);

static void read_callback(bool &data_available,asio::deadline_timer& timeout,size_t bytes_transferred,const system::error_code& error)
{
	if (error || !bytes_transferred)
	{
		cerr << "read callback error = " << error << endl;
		data_available = false;
		return;
	}

	//getting to this point means check_callback successfully recognized packet header with enough data.
	data_available = true;

	//timeout can now be canceled.
	timeout.cancel(); 
}

static uint8_t* find_packet_begin(uint8_t * data,size_t len)
{
	uint8_t *begin = data;
	while(len-- && *begin != FBGN) begin++;
	if(*begin != FBGN) return 0;
	return begin;
}

static bool check_callback(uint8_t *b,PacketHeader *header,size_t header_len,size_t bytes_transferred,const system::error_code& error)
{
	if(error) {
		cerr << "check callback error = " << error << endl;
		return true;
	}
	
	//if(log_level) debug_data("Check callback bytes",b,bytes_transferred);
	
	uint8_t* packet = find_packet_begin(b,bytes_transferred);
	if(!packet) return false;
	
	size_t current_length = bytes_transferred - (packet - b);
	//if(log_level) debug_data("Selected",packet,current_length);

	if(current_length <= sizeof(PacketHeader)) return false;

	size_t buf_len = unbytestaff(header,header_len,packet,current_length);
	//if(log_level) debug_data("Unbytestaffed",header,buf_len);

	if(buf_len < header->full_size()) return false;

	return true;
}

static void wait_callback(asio::serial_port& serial, const system::error_code& error)
{
  if (error) return;   // Data was read and this timeout was canceled
  serial.cancel();     // will cause read_callback to fire with an error
}

class AsioImpl : public IReaderImpl
{
	asio::io_service io_svc;
	asio::serial_port serial;
	asio::deadline_timer timeout;
public:
	AsioImpl(const char* path,uint32_t baud):serial(io_svc),timeout(io_svc) {
		
		serial.open( path );

		const asio::serial_port_base::parity parity_opt(asio::serial_port_base::parity::none);
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

	long transceive(void* data,size_t len,void* packet,size_t packet_len);
};

long AsioImpl::transceive(void* data,size_t len,void* packet,size_t packet_len)
{
	namespace ph = boost::asio::placeholders;

	unsigned char read_buf[512] = {0};
	unsigned char write_buf[512] = {0};
	size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

	bool data_available = false;
		
	if(log_level) debug_data("send_command[send]",write_buf,write_buf_len);

	PacketHeader *header = (PacketHeader*)packet;
	try {
		asio::write(this->serial,asio::buffer(write_buf,write_buf_len));
		
		//there are 3 callbacks in total:
		//1. check_callback - called every time new portion of data comes from serial port and 
		//   decide whether received data can be called packet - if it can, this callback returns
        //   true and read_callback is called
		//2. read_callback - this callback sets data_available flag and cancels timeout callback to
		//   prevent its calling. After that, since there is no more callbacks pending, io_svc.run() completes.
		asio::async_read(this->serial,asio::buffer(read_buf),
			bind(&check_callback,read_buf,header,packet_len,
				 ph::bytes_transferred,ph::error),
			bind(&read_callback,ref(data_available),ref(this->timeout),
				 ph::bytes_transferred,ph::error));

		//3. wait_callback - fires when maximum packet waiting time expired. If its called, that means not 
		//   enough data came from the serial port to be recognized as a complete packet - so, serial port
		//   operations are canceled and io_svc run completes without data_available flag set.
		//   Serial callbacks cancel leads to calling read_callback and wait_callback with error flag.
		this->timeout.expires_from_now(posix_time::milliseconds(PACKET_RECEIVE_TIMEOUT));
		this->timeout.async_wait(bind(&wait_callback, ref(this->serial),ph::error));

		this->io_svc.run();  // will block until async callbacks are finished
		this->io_svc.reset(); //prepare for the next round
	} catch(boost::system::system_error& e) {
		cerr << e.what() << endl;
		return IO_ERROR;
	}

	if(!data_available) return NO_ANSWER;
		
	return 0;
}

IReaderImpl* create_asio_impl(const char* path,uint32_t baud)
{
	return new AsioImpl(path,baud);
}