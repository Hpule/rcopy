# Makefile for CPE464 tcp and udp test code
# updated by Hugh Smith - April 2023

CC= gcc
CFLAGS= -g -Wall -pedantic 
LIBS = 

# Include new object files (helperFunctions.o and windowBuffer.o)
OBJS = networks.o gethostbyname.o pollLib.o safeUtil.o helperFunctions.o windowBuffer.o

# Uncomment next two lines if you're using sendtoErr() library
LIBS += libcpe464.2.21.a -lstdc++ -ldl
CFLAGS += -D__LIBCPE464_

all: udpAll

udpAll: rcopy server

rcopy: rcopy.c $(OBJS) 
	$(CC) $(CFLAGS) -o rcopy rcopy.c $(OBJS) $(LIBS)

server: server.c $(OBJS) 
	$(CC) $(CFLAGS) -o server server.c $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

cleano:
	rm -f *.o

clean:
	rm -f rcopy server *.o
