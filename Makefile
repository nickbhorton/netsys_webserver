CC=gcc
CFLAGS=-g3 -Wall -Werror -fsanitize=address

all: test 

test: test.o common.o
	$(CC) -o $@ $^ $(CFLAGS) -lcunit

test.o: test.c
common.o: common.c common.h
