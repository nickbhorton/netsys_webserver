CC=gcc
CFLAGS=-g3 -Wall -Werror -fsanitize=address

all: test server

.PHONY: all
	

test: test.o common.o debug_macros.o
	$(CC) -o $@ $^ $(CFLAGS) -lcunit

server: server.o common.o debug_macros.o
	$(CC) -o $@ $^ $(CFLAGS)

test.o: test.c
common.o: common.c common.h
debug_macros.o: debug_macros.c debug_macros.h
server.o: server.c
