CC=gcc
CFLAGS=-Wall -g

build:
	make cs352proxy.o packets.o helper.o cs352proxy

cs352proxy.o: cs352proxy.c
	$(CC) $(CFLAGS) -c cs352proxy.c

cs352proxy: cs352proxy.o
	$(CC) $(CFLAGS) cs352proxy.o packets.o helper.o -pthread -o cs352proxy

packets.o: packets.c
	$(CC) $(CFLAGS) -c packets.c

helper.o: helper.c
	$(CC) $(CFLAGS) -c helper.c

clean:
	rm -rf *.o cs352proxy *.dSYM

rebuild:
	make clean build
