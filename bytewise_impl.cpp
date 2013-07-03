#include "protocol.h"

#include <windows.h>
#include <time.h>

void HandleLastError(const char *msg);
HANDLE ComOpen(const char* path, uint32_t baud,uint32_t flags);

class BytewiseImpl : public IReaderImpl
{
	HANDLE hCom;
public:
	BytewiseImpl(const char* path,uint32_t baud) {
		//fprintf(stderr,"BytewiseImpl\n");
		hCom = ComOpen(path,baud,0);
	}

	virtual ~BytewiseImpl() {
		if (hCom != NULL) CloseHandle(hCom);
		hCom = NULL;		
	}

	long transceive(void* data,size_t len,void* packet,size_t packet_len) {
		unsigned char write_buf[512] = {0};

		//__int64 frequency;
		//QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);

		size_t write_buf_len = bytestaff(write_buf,sizeof(write_buf),data,len);

		PurgeComm(hCom,PURGE_TXCLEAR|PURGE_RXCLEAR);
	
		DWORD bytes_written = 0;
		BOOL write_ret = WriteFile(hCom,write_buf,write_buf_len,&bytes_written,0);

		/*COMMTIMEOUTS		CommTimeouts;
		CommTimeouts.ReadIntervalTimeout			= 100;
		CommTimeouts.ReadTotalTimeoutMultiplier		= 100;
		CommTimeouts.ReadTotalTimeoutConstant		= 100;
		CommTimeouts.WriteTotalTimeoutMultiplier	= 100;
		CommTimeouts.WriteTotalTimeoutConstant		= 100;
		SetCommTimeouts(hCom, &CommTimeouts);*/

		//__int64 a;
		//QueryPerformanceCounter((LARGE_INTEGER*)&a);

		time_t timeLimit, currentTime;
		time(&timeLimit);
		timeLimit += 10;

		DWORD nb_read;
		uint8_t input;
		size_t bytes_read = 0;
		uint8_t escape = 0;
		PacketHeader* header = (PacketHeader*)packet;			
		uint8_t* read_buf = (uint8_t*)packet; 
	
		while(true) {
			time(&currentTime);
			if (currentTime>=timeLimit)	return IO_ERROR;

			ReadFile(hCom,&input, 1, &nb_read, NULL);
			
			if(nb_read == 0 || input != FBGN) continue;

			read_buf[bytes_read++] = input;

			while(true) {
				ReadFile(hCom, &input, 1, &nb_read, NULL);
				if(nb_read == 0 || bytes_read >= packet_len) return IO_ERROR;

				if(input == FESC) escape = 1;
				else {
					if( escape == TRUE ) { // если предыдущим принят ESC байт  
						escape = FALSE;
						if(input == TFBGN) input = FBGN;
						else if(input == TFESC) input = FESC;
					}
					read_buf[bytes_read++] = input;

					if(bytes_read >= sizeof(PacketHeader) && bytes_read >= header->full_size()) {
						/*__int64 b;
						QueryPerformanceCounter((LARGE_INTEGER*)&b);
						long elapsed = (long)(((double)(b - a) / frequency) * 1000000);
						fprintf(stderr,"perfc: %i\n",elapsed);*/
						
						return 0;
					}
				}
			}
		}
		
		/*__int64 b;
		QueryPerformanceCounter((LARGE_INTEGER*)&b);
		long elapsed = (long)(((double)(b - a) / frequency) * 1000000);
		fprintf(stderr,"perfc: %i\n",elapsed);*/
	}
};

IReaderImpl* create_bytewise_impl(const char* path,uint32_t baud)
{
	return new BytewiseImpl(path,baud);
}