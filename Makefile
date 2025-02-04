CC=gcc
CFLAGS=-g3 -Wall -Werror -fsanitize=address

all: test 

test: test.o server.o
	$(CC) -o $@ $^ $(CFLAGS) -lcunit

test.o: test.c
server.o: server.c server.h
