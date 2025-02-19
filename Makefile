CC=gcc
CFLAGS_DEBUG=-g -Wall -Werror -fsanitize=address
CFLAGS_RELEASE=-O3 -Wall -Werror
CFLAGS=$(CFLAGS_DEBUG)

all: test server

.PHONY: all
	

test: unit_test.o common.o debug_macros.o String.o
	$(CC) -o $@ $^ $(CFLAGS) -lcunit

server: server.o common.o debug_macros.o String.o
	$(CC) -o $@ $^ $(CFLAGS)

unit_test.o: unit_test.c
common.o: common.c common.h
debug_macros.o: debug_macros.c debug_macros.h
String.o: String.c String.h
server.o: server.c

clean:
	rm -f *.o
	rm -f test
	rm -f server
	rm -f aria2c.log
