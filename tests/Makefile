CC=gcc
CFLAGS=-Og -g -Wall -Wextra -std=c99 -I..
CFLAGS_RELEASE=-Ofast -fno-plt -flto -march=native -DNDEBUG
# CFLAGS+=-fsanitize=undefined -fsanitize=address

all: testsuite

bench: benchmark
	$(RUNNER) ./benchmark

test: testsuite
	$(RUNNER) ./testsuite

testsuite: testsuite.c ../minialloc.h Makefile
	$(CC) $(CFLAGS) testsuite.c -o testsuite

benchmark: benchmark.c ../minialloc.h Makefile
	$(CC) $(CFLAGS) $(CFLAGS_RELEASE) benchmark.c -o benchmark

clean:
	rm -f testsuite
