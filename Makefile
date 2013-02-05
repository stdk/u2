CFLAGS = -O2 -Wall -fPIC

all: u2

u2: asio_impl.o contract.o protocol.o reader.o card.o
	g++ -shared -fPIC $^ -o $@

%.o: %.cpp
	g++ $(CFLAGS)  -c $^ -o $@ 

clean:
	rm -f *.o 

