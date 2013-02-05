CFLAGS = -O2 -Wall -fPIC

all: u2

u2: crc16.o asio_impl.o contract.o protocol.o reader.o card.o
	g++ -shared -Wl,-soname,libu2.so -Wl,--no-undefined $^ -o $@

%.o: %.cpp
	g++ $(CFLAGS)  -c $^ -o $@ 

clean:
	rm -f *.o 

