CC=gcc
CFLAGS=-Wall -pthread -std=c++11 -g -c -I../
VPATH=../
DEPS = RDMASenderReceiver.h RDMAConn.h RDMAUtils.h ringbuffer.h log.h SockUtils.h thpool.h

objects = RDMASenderReceiverBP.o RDMASenderReceiverEvent.o RDMASenderReceiver.o \
			RDMAConnBP.o RDMAConnEvent.o RDMAConn.o \
			RDMAUtils.o ringbuffer.o log.o thpool.o
LDFLAGS += -pthread -ldl -lm -libverbs -g

all: RDMAServer RDMAClient

%.o: %.cc $(DEPS)
	$(CXX) -c -o $@ $< $(CFLAGS)

RDMAServer: $(objects) RDMAServer.o
	$(CXX) $^ $(LDFLAGS) -o $@

RDMAClient: $(objects) RDMAClient.o
	$(CXX) $^ $(LDFLAGS) -o $@

.PHONY: clean

clean:
	-rm *.o