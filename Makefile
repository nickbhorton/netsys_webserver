CC=gcc

CFLAGS=-Wall -Werror
CFLAGS_DEBUG=-g -fsanitize=address
CFLAGS_PROFILE =-g -O3
CFLAGS_RELEASE=-O3 -DDebugPrint=0

debug: CFLAGS += $(CFLAGS_DEBUG)
profile: CFLAGS += $(CFLAGS_PROFILE)
release: CFLAGS += $(CFLAGS_RELEASE)
all: CFLAGS += $(CFLAGS_RELEASE)

all: server

debug: server unit_test
	./unit_test
 
profile: server

release: server

install: server
	cp server nhws
	mv nhws ~/opt/bin

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
