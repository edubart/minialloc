CC=gcc
CFLAGS=-Og -g -Wall -Wextra -std=c89 -I..
# CFLAGS+=-fsanitize=undefined -fsanitize=address
CFLAGS+=-O2

all: testsuite

test: testsuite
	$(RUNNER) ./testsuite

testsuite: testsuite.c ../minialloc.h Makefile
	$(CC) $(CFLAGS) testsuite.c -o testsuite

clean:
	rm -f testsuite
