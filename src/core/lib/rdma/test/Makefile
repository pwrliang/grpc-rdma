CC=gcc
CXX = g++
CFLAGS=-Wall -Wformat=0 -pthread -std=c++11 -g -c -I../
CXXFLAGS += -std=c++11 -g
VPATH=../
DEPS = RDMASenderReceiver.h RDMAConn.h RDMAUtils.h ringbuffer.h log.h SockUtils.h thpool.h Utils.h

objects = RDMASenderReceiverBP.o RDMASenderReceiverEvent.o RDMASenderReceiver.o \
			RDMAConnBP.o RDMAConnEvent.o RDMAConn.o \
			RDMAUtils.o ringbuffer.o log.o thpool.o
LDFLAGS += -pthread -ldl -lm -libverbs -g

all: RDMAServer RDMAClient

%.o: %.cc $(DEPS)
	$(CXX) -g -gdwarf-4 -c -o $@ $<

RDMAServer: $(objects) RDMAServer.o
	$(CXX) -g -gdwarf-4 $^ $(LDFLAGS) -o $@

RDMAClient: $(objects) RDMAClient.o
	$(CXX) -g -gdwarf-4 $^ $(LDFLAGS) -o $@

.PHONY: clean

clean:
	rm -f *.o RDMAServer RDMAClient