#include "protocol.h"

#include <windows.h>
#include <time.h>

#include <cstdio>

void HandleLastError(const char *msg);
size_t unbytestaff(void* dst_buf,size_t dst_len,void *src_buf,size_t src_len);
size_t bytestaff(void *dst_buf, size_t dst_len, void *src_buf,size_t src_len);
HANDLE ComOpen(const char* path, uint32_t baud);

class BlockwiseImpl : public IReaderImpl
{
	HANDLE hCom;
public:
	BlockwiseImpl(const char* path,uint32_t baud) {
		fprintf(stderr,"BlockwiseImpl\n");
		hCom = ComOpen(path,baud);
		if(INVALID_HANDLE_VALUE == hCom) throw -1;
	}

	virtual ~BlockwiseImpl() {
		if (hCom != NULL) CloseHandle(hCom);
		hCom = NULL;		
	}

	long transceive(void* data,size_t len,void* packet,size_t packet_len) {
		unsigned char read_buf[512] = {0};
		unsigned char write_buf[512] = {0};

		size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

		PurgeComm(hCom,PURGE_TXCLEAR|PURGE_RXCLEAR);
	
		DWORD bytes_written = 0;
		BOOL write_ret = WriteFile(hCom,write_buf,write_buf_len,&bytes_written,0);
/*
		COMMTIMEOUTS		CommTimeouts;
		CommTimeouts.ReadIntervalTimeout			= 500;
		CommTimeouts.ReadTotalTimeoutMultiplier		= 500;
		CommTimeouts.ReadTotalTimeoutConstant		= 500;
		CommTimeouts.WriteTotalTimeoutMultiplier	= 500;
		CommTimeouts.WriteTotalTimeoutConstant		= 500;
		SetCommTimeouts(hCom, &CommTimeouts);
*/
		time_t timeLimit, currentTime;
		time(&timeLimit);
		timeLimit += 2;
		
		while(true) {
			//fprintf(stderr,"X");
			time(&currentTime);
			if (currentTime>=timeLimit)	return IO_ERROR;

			DWORD bytes_read;
			ReadFile(hCom,read_buf, 1, &bytes_read, NULL);

			if(bytes_read == 1 && *read_buf == FBGN) break;
		}

		DWORD bytes_read = 1;
		DWORD errors = 0;
		COMSTAT comstat = {0};
		PacketHeader* header = (PacketHeader*)packet;			

		while(true) {		
			//fprintf(stderr,"Y");
			time(&currentTime); //does not decrease perfomance
			if (currentTime>=timeLimit)	return IO_ERROR;

			ClearCommError(hCom,&errors,&comstat);
			if(!comstat.cbInQue) {
				//Sleep(0); //leads to slight perfomance decrease
				continue;
			}

			DWORD bytes_read_current = 0;
			ReadFile(hCom,read_buf + bytes_read,comstat.cbInQue,&bytes_read_current,NULL);
			bytes_read += bytes_read_current;

			size_t buf_len = unbytestaff(packet,packet_len,read_buf,bytes_read);
					
			if(buf_len >= header->full_size()) break;
		}
		
		//checking packet validity
		if(!header->crc_check()) return PACKET_CRC_ERROR;
		if(header->code == NACK_BYTE) {
			return header->nack_data();
		}

		return 0;
	}
};

IReaderImpl* create_blockwise_impl(const char* path,uint32_t baud)
{
	return new BlockwiseImpl(path,baud);
}

/*long transceive_aio(void* data,size_t len,void* packet,size_t packet_len) {
		unsigned char read_buf[512] = {0};
		unsigned char write_buf[512] = {0};
		size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

		PurgeComm(hCom,PURGE_TXCLEAR|PURGE_RXCLEAR);

		OVERLAPPED writeAIO = {0};

		DWORD bytes_written = 0;
		BOOL write_ret = WriteFile(hCom,write_buf,write_buf_len,0,&writeAIO);
		if(!write_ret && GetLastError() != ERROR_IO_PENDING) {
			HandleLastError("WriteFile");
			return IO_ERROR;
		} else if(!write_ret && GetLastError() == ERROR_IO_PENDING) {
			while(!GetOverlappedResult(hCom,&writeAIO,&bytes_written,TRUE)) {
			    DWORD err = GetLastError();
				if(err == ERROR_IO_INCOMPLETE) {
					fprintf(stderr,"Write pending\n",GetLastError());
				} else {
					HandleLastError("GetOverlappedResult");
					return IO_ERROR;
				}				
			}
			if(bytes_written != write_buf_len) {
				fprintf(stderr,"bytes_written != write_buf_len\n");
				return IO_ERROR;
			}
		}

		OVERLAPPED readAIO = {0};
		DWORD bytes_read = 0;

		BOOL read_ret = ReadFile(hCom,read_buf,sizeof(read_buf), NULL,&readAIO);
		if (!read_ret && GetLastError() != ERROR_IO_PENDING ) {
            HandleLastError("ReadFile");
            return IO_ERROR;
        } else if(!read_ret && GetLastError() == ERROR_IO_PENDING) {
			while( !GetOverlappedResult( hCom,&readAIO,&bytes_read,TRUE)) {
				if (GetLastError() == ERROR_IO_INCOMPLETE) {
					fprintf(stderr,"Read pending\n",GetLastError());
				} else {
					HandleLastError("GetOverlappedResult");
                    break;
				}
			}
			fprintf(stderr,"bytes_read: %i\n",bytes_read);
		}

		uint8_t* packet_begin = find_packet_begin(read_buf,bytes_read);
		if(!packet) return IO_ERROR;
	
		size_t current_length = bytes_read - (packet_begin - read_buf);
		//debug_data("Selected",packet_begin,current_length);

		if(current_length <= sizeof(PacketHeader)) return IO_ERROR;

		size_t buf_len = unbytestaff(packet,packet_len,packet_begin,current_length);
		//debug_data("Unbytestaffed",packet,buf_len);

		PacketHeader* header = (PacketHeader*)packet;
		if(buf_len < header->full_size()) return IO_ERROR;

		//checking packet validity
		if(!header->crc_check()) return PACKET_CRC_ERROR;
		if(header->code == NACK_BYTE) {
			//cerr << "header->code ==  NACK_BYTE" << endl;
			return header->nack_data();
		}

		return 0;
	}*/