CFLAGS = -O2 -Wall -fPIC

all: libu2.so

libu2.so: crc16.o card_storage.o asio_impl.o asio_mt_impl.o file_impl.o contract.o protocol.o reader.o card.o transport.o subway_protocol.o cp210x_impl.o tcp_impl.o
	g++ -shared -Wl,-soname,libu2.so -Wl,--no-undefined  -lboost_system -lboost_thread -lpthread -lakemi_usb -lrt $^ -o $@

%.o: %.cpp
	g++ $(CFLAGS) -I ../usb/akemi/inc -std=c++0x  -c $^ -o $@ 

clean:
	rm -f *.o 

