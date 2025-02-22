CC=gcc

CFLAGS=-Wall -Werror
CFLAGS_DEBUG=-g -fsanitize=address
CFLAGS_PROFILE =-g -O3
CFLAGS_RELEASE=-O3

all: server unit_test

debug: CFLAGS += $(CFLAGS_DEBUG)
profile: CFLAGS += $(CFLAGS_PROFILE)
release: CFLAGS += $(CFLAGS_RELEASE)

debug: server unit_test
 
profile: server unit_test

release: server unit_test

.PHONY: all debug profile release

unit_test: unit_test.o common.o
	$(CC) -o $@ $^ $(CFLAGS) -lcunit

server: server.o common.o
	$(CC) -o $@ $^ $(CFLAGS)

unit_test.o: unit_test.c
common.o: common.c common.h
server.o: server.c

clean:
	rm -f *.o
	rm -f test
	rm -f server
	rm -f aria2c.log
	rm -f callgrind*
