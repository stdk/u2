#include "protocol.h"

#include <windows.h>
#include <time.h>

#include <cstdio>

void HandleLastError(const char *msg);
HANDLE ComOpen(const char* path, uint32_t baud,uint32_t flags);
void HandleLastError(const char *msg);

#define IO_BUFFER_LEN   512
#define IO_TIMEOUT      500
#define FULL_TIMEOUT    2000

#define USE_IOCP

class BlockwiseImpl : public IReaderImpl
{
	HANDLE hCom;
#ifdef USE_IOCP
	HANDLE hIOCP;
	OVERLAPPED hOver;
#endif
public:
	BlockwiseImpl(const char* path,uint32_t baud) {
		fprintf(stderr,"BlockwiseImpl\n");
#ifdef USE_IOCP
		uint32_t flags = FILE_FLAG_OVERLAPPED;
#else
		uint32_t flags = 0;
#endif

		hCom = ComOpen(path, baud, flags);
		if(INVALID_HANDLE_VALUE == hCom) throw -1;
		
#ifdef USE_IOCP
		hIOCP = CreateIoCompletionPort(hCom,0,0,0);
		if(!hIOCP) {
			HandleLastError("CreateIoCompletionPort");
			throw -2;
		};
	
		memset(&hOver,0,sizeof(hOver));
#endif
	}

	virtual ~BlockwiseImpl() {
#ifdef USE_IOCP
		if(hIOCP) {
			CloseHandle(hIOCP);
			hIOCP = 0;
		}
#endif
		if (hCom) {
			CloseHandle(hCom);
			hCom = NULL;		
		}		
	}

	BOOL write(void *buf,size_t len,DWORD *bytes_written)
	{
#ifdef USE_IOCP
		OVERLAPPED *over = &hOver;
#else
		OVERLAPPED *over = 0;
#endif

		BOOL write_ret = WriteFile(hCom, buf, len, bytes_written, over);
		if(!write_ret && GetLastError() != ERROR_IO_PENDING) {
			HandleLastError("WriteFile");
			return FALSE;
		}
#ifdef USE_IOCP	
		OVERLAPPED *iocpOver;
		ULONG iocpKey;

		BOOL completed = GetQueuedCompletionStatus(hIOCP,bytes_written,&iocpKey,&iocpOver,IO_TIMEOUT);
		if(!completed) {
			HandleLastError("WriteFile:GetQueuedCompletionStatus");
			return FALSE;
		}
#endif
		return TRUE;
	}

	BOOL read(void *buf,size_t len,DWORD *bytes_read)
	{
#ifdef USE_IOCP
		OVERLAPPED *over = &hOver;
#else
		OVERLAPPED *over = 0;
#endif

		BOOL read_ret = ReadFile(hCom, buf, len, bytes_read, over);
		if(!read_ret && GetLastError() != ERROR_IO_PENDING) {
			HandleLastError("ReadFile");
			return FALSE;
		}

#ifdef USE_IOCP	
		OVERLAPPED *iocpOver;
		ULONG iocpKey;

		BOOL completed = GetQueuedCompletionStatus(hIOCP,bytes_read,&iocpKey,&iocpOver,IO_TIMEOUT);
		if(!completed) {
			HandleLastError("ReadFile:GetQueuedCompletionStatus");
			return FALSE;
		}
#endif

		return TRUE;
	}

	long transceive(void* data,size_t len,void* packet,size_t packet_len) {
		unsigned char read_buf[IO_BUFFER_LEN]  = {0};
		unsigned char write_buf[IO_BUFFER_LEN] = {0};

		size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

		//PurgeComm(hCom,PURGE_TXCLEAR|PURGE_RXCLEAR);
	
		DWORD bytes_written = 0;
		if(!write(write_buf,write_buf_len,&bytes_written)) return IO_ERROR;

		DWORD time_limit = GetTickCount() + FULL_TIMEOUT;

		DWORD bytes_read = 0;
		while(GetTickCount() < time_limit && !bytes_read && *read_buf != FBGN) {
			if(!read(read_buf,1,&bytes_read)) return IO_ERROR;
		}
		
		DWORD errors = 0;
		COMSTAT comstat = {0};

		while(GetTickCount() < time_limit) {		
			ClearCommError(hCom,&errors,&comstat);
			if(!comstat.cbInQue) continue;

			DWORD bytes_read_current = 0;
			if(!read(read_buf+bytes_read,comstat.cbInQue,&bytes_read_current)) return IO_ERROR;
			bytes_read += bytes_read_current;

			size_t buf_len = unbytestaff(packet,packet_len,read_buf,bytes_read);
					
			if(buf_len >= ((PacketHeader*)packet)->full_size()) return 0;
		}
		
		return TIMEOUT_ERROR;
	}
};

IReaderImpl* create_blockwise_impl(const char* path,uint32_t baud)
{
	return new BlockwiseImpl(path,baud);
}