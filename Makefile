CFLAGS = -O2 -Wall -fPIC

all: libu2.so

libu2.so: crc16.o card_storage.o asio_impl.o asio_mt_impl.o file_impl.o contract.o protocol.o reader.o card.o transport.o 
	g++ -shared -Wl,-soname,libu2.so -Wl,--no-undefined -lboost_system -lboost_thread -lpthread $^ -o $@

%.o: %.cpp
	g++ $(CFLAGS)  -c $^ -o $@ 

clean:
	rm -f *.o 

