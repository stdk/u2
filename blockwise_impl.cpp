#include "protocol.h"
#include "custom_combiners.h"

#include <boost/thread.hpp>
#include <boost/system/windows_error.hpp>
#include <boost/signals2.hpp>
#include <algorithm>

#define NOMINMAX
#include <windows.h>
#include <time.h>

#include <cstdio>

static const int log_level = 0;

#define IO_BUFFER_LEN   512
#define IO_TIMEOUT      500
#define FULL_TIMEOUT    2000

#define OP_READ  1
#define OP_WRITE 2

#define KEY_STOP 0xFFFFFFFF

static DWORD HandleLastError(const char *msg) {
	DWORD lastError = GetLastError();
	char *err;
    DWORD ret = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			0,
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
			(char*)&err,
			0,
			NULL);
		
	if(!ret) {
		fprintf(stderr,"FormatMessage(%s,%i): %i (%i)\n",msg,lastError,ret,GetLastError());
		return lastError;
	}

	static char buffer[1024];
	_snprintf_s(buffer, sizeof(buffer), "%s(%i): %s", msg,lastError,err);
    fprintf(stderr,buffer);
    LocalFree(err);

	return lastError;
}

static HANDLE ComOpen(const char *path, uint32_t baud, uint8_t parity, uint32_t flags) {
	COMMTIMEOUTS		CommTimeouts;
	DCB					dcb;
		
	wchar_t baud_s[50];
	wsprintf(baud_s, L"baud=%d parity=%s data=8 stop=1",baud,
		parity == PARITY::EVEN ? "E" : (parity == PARITY::ODD ? "O" : "N" ) );

	// get a handle to the port
	HANDLE hCom = CreateFileA(path,					// communication port path
				     GENERIC_READ | GENERIC_WRITE,	// read/write types
				     0,								// comm devices must be opened with exclusive access
				     NULL,							// no security attributes
				     OPEN_EXISTING,					// comm devices must use OPEN_EXISTING
				     flags,    	                    // flags
				     0);							// template must be 0 for comm devices

	
	if (hCom == INVALID_HANDLE_VALUE) {
		HandleLastError("CreateFile");
		return hCom;
	}

	// set the timeout values
	CommTimeouts.ReadIntervalTimeout			= 1;
	CommTimeouts.ReadTotalTimeoutMultiplier		= 0;
	CommTimeouts.ReadTotalTimeoutConstant		= 0;
	CommTimeouts.WriteTotalTimeoutMultiplier	= 0;
	CommTimeouts.WriteTotalTimeoutConstant		= 0;

	// configure
	if (SetCommTimeouts(hCom, &CommTimeouts))
	{					   
		//if (GetCommState(hCom, &dcb))
		//{
			//dcb.fOutxCtsFlow = FALSE;
			
			//dcb.fRtsControl = RTS_CONTROL_ENABLE;
			
			if (BuildCommDCB(baud_s, &dcb))
			{
				
				
				SetCommState(hCom, &dcb);			// normal operation... continue
			}
		//}
	}

	return hCom;
}

class Timeout
{
	bool active;
	size_t begin;
	size_t timeout;
	function<void ()> callback;
public:
	Timeout():active(false) {

	}

	void set(size_t _timeout,function<void ()> timeout_callback) {
		active = true;
		callback = timeout_callback;
		begin = GetTickCount();
		timeout = _timeout;		
	}

	bool check() {
		if(active) {
			if(!time_left()) {
				if(!callback.empty()) {
					callback();
					callback.clear();
				}
				active = false;
				return true;
			}
		}
		return false;
	}

	void cancel() {
		active = false;
		callback.clear();
	}

	bool is_active() const {
		return active;
	}
    
	//returns time left on this timeout or 0 when there is no time left
	size_t time_left() {
		size_t diff = GetTickCount() - begin;
		return diff < timeout ? timeout - diff : 0;
	}

	operator DWORD() {
		return active ? time_left() : INFINITE;
	}
};

class BlockwiseImpl : public IOProvider
{
	thread io_thread;

	struct OVER
	{
		OVERLAPPED hOver;
		DWORD operation;

		OVER(DWORD op):operation(op) {
			memset(&hOver,0,sizeof(hOver));
		}
	};

	OVER read_over;
	OVER write_over;

	HANDLE hCom;
	HANDLE hIOCP;

	uint8_t read_buf[512];
	size_t read_size;

	// Using maximum combiner assures that if more than one listener will be active
	// on this signal, we should always get maximum of their return values.
	// As a result, we will correctly stop io_service when at least one listener got what it wanted.
	signals2::signal<long (void *data, size_t len),combiner::maximum<long>> data_received;
	signals2::signal<long (size_t bytes_transferred, const system::error_code&), combiner::maximum<long>> data_sent;

	Timeout timeout;
public:
	BlockwiseImpl(const char* path,uint32_t baud, uint8_t parity)
	:read_over(OP_READ),write_over(OP_WRITE),read_size(1) {
		uint32_t flags = FILE_FLAG_OVERLAPPED;
		hCom = ComOpen(path, baud, parity, flags);
		if(INVALID_HANDLE_VALUE == hCom) throw -1;
		
		hIOCP = CreateIoCompletionPort(hCom,0,0,0);
		if(!hIOCP) {
			HandleLastError("CreateIoCompletionPort");
			throw -2;
		};
	
		io_thread = thread(bind(&BlockwiseImpl::iocp_thread,this));		
	}

	void stop_iocp_thread()
	{
		CancelIo(hCom);
		PostQueuedCompletionStatus (hIOCP,0,KEY_STOP,0);
		io_thread.join();
	}

	virtual ~BlockwiseImpl() {
		stop_iocp_thread();		

		if(hIOCP) {
			CloseHandle(hIOCP);
			hIOCP = 0;
		}

		if (hCom) {
			CloseHandle(hCom);
			hCom = NULL;		
		}
	}

	void iocp_thread()
	{
		DWORD bytes_transferred;
		OVER *over;
		ULONG key;

		while(true) {
			bytes_transferred = 0;
			BOOL completed = GetQueuedCompletionStatus(hIOCP,&bytes_transferred,&key,(OVERLAPPED**)&over,timeout);

			if(key == KEY_STOP) {
				if(log_level) std::cerr << "KEY_STOP" << std::endl;
				break;
			}

			DWORD last_error = completed ? 0 : HandleLastError("GetQueuedCompletionStatus");
			if(log_level) fprintf(stderr,"completed[%i] bytes_transferred[%i] over.operation[%i]\n",completed,bytes_transferred,over ? over->operation : 0);			
			
			if((!completed && last_error == WAIT_TIMEOUT)) {
				fprintf(stderr,"WAIT_TIMEOUT\n");				
			}

			if(timeout.check()) {
				int i = CancelIo(hCom);
				fprintf(stderr,"CancelIo = %i\n",i);
				continue;
			}
			

			if(!completed || !over) {
				continue;
			}

			if(over->operation == OP_READ) {
				if(completed) {
					long packet_found = data_received(read_buf,bytes_transferred);
					if(!packet_found) {
						Read(read_buf,1);
					}
				}
			}

			if(over->operation == OP_WRITE) {
				if(data_sent(bytes_transferred,system::error_code(last_error,system::system_category())) == 0) {
					Read(read_buf,1);
				}
			}						
		}

		if(log_level) std::cerr << "iocp_thread stopped" << std::endl;
	}

	BOOL Write(void *buf, size_t len)
	{
		BOOL write_ret = WriteFile(hCom, buf, len, 0, (OVERLAPPED*)&write_over);
		if(!write_ret && GetLastError() != ERROR_IO_PENDING) {
			HandleLastError("WriteFile");
			return FALSE;
		}

		return TRUE;
	}

	bool Read(void *buf, size_t len)
	{
		//ReadFile
		//If the function succeeds, the return value is nonzero (TRUE).
		//If the function fails, or is completing asynchronously, the return value is zero (FALSE). To get extended error information, call the GetLastError function.
		//Note  The GetLastError code ERROR_IO_PENDING is not a failure; it designates the read operation is pending completion asynchronously. For more information, see Remarks.
		//Comments:
		//On Windows XP (at least) we have ReadFile returning TRUE and still initiating async iocp operation.

		if(log_level) std::cerr << "BlockwiseImpl::Read " << timeout.time_left()<< std::endl;
		BOOL read_ret = ReadFile(hCom, buf, len, 0, (OVERLAPPED*)&read_over);
		if(!read_ret && GetLastError() != ERROR_IO_PENDING) {
			HandleLastError("ReadFile");
			return true;
		} 
		return false;;
	}

	static void disconnector(signals2::connection c)
	{
		if(log_level) std::cerr << "BlockwiseImpl::disconnector" << std::endl;

		c.disconnect();
	}

	function<void ()> listen(IOProvider::listen_callback callback)
	{
		if(log_level) std::cerr << "BlockwiseImpl::listen" << std::endl;

		signals2::connection c = data_received.connect(callback);
		return bind(disconnector,c);
	}

	static long write_callback(signals2::connection c,IOProvider::send_callback callback,
		                       size_t bytes_transferred,const system::error_code &error)
	{
		if(log_level) std::cerr << "BlockwiseImpl::write_callback" << std::endl;
		c.disconnect();
		return callback(bytes_transferred,error);		
	}

	virtual void send(void *data, size_t len, IOProvider::send_callback callback) 
	{
		if(log_level) std::cerr << "BlockwiseImpl::send" << std::endl;
		data_sent.connect_extended(bind(write_callback,_1,callback,_2,_3));
		if(!Write(data,len)) {
			data_sent(0,system::error_code(::GetLastError(),system::system_category()));
		}
	}

	virtual long set_timeout(size_t time, function<void ()> callback)
	{
		if(log_level) std::cerr << "BlockwiseImpl::set_timeout" << std::endl;

		timeout.set(time,callback);

		return 0;
	}

	virtual long cancel_timeout()
	{
		if(log_level) std::cerr << "BlockwiseImpl::cancel_timeout" << std::endl;
		timeout.cancel();
		return 0;
	}
};

IOProvider* create_blockwise_impl(const char* path,uint32_t baud,uint8_t parity)
{
	return new BlockwiseImpl(path,baud,parity);
}