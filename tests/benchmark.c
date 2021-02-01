#define MINIALLOC_IMPL
#include "minialloc.h"

#include <stdio.h>
#include <assert.h>
#include <x86intrin.h>
mal_allocator allocator;

#define N 10000000
void* ptrs[N];

void bench_minialloc() {
  int i;
  unsigned long start, end;

  start = __rdtsc();
  for(i=0;i<N;++i) {
    int size = (i % 64) + 1;
    ptrs[i] = mal_alloc(&allocator, size);
  }
  end = __rdtsc();
  printf("alloc %lu\n", (end - start)/N);

  start = __rdtsc();
  for(i=0;i<N;++i) {
    int size = (i % 64) + 1;
    ptrs[i] = mal_realloc(&allocator, ptrs[i], size*2, size);
  }
  end = __rdtsc();
  printf("realloc %lu\n", (end - start)/N);

  start = __rdtsc();
  for(i=0;i<N;++i) {
    mal_dealloc(&allocator, ptrs[i]);
  }
  end = __rdtsc();
  printf("dealloc %lu\n", (end - start)/N);
}

void bench_malloc() {
  int i;
  unsigned long start, end;

  start = __rdtsc();
  for(i=0;i<N;++i) {
    int size = (i % 64) + 1;
    ptrs[i] = malloc(size);
  }
  end = __rdtsc();
  printf("alloc %lu\n", (end - start)/N);

  start = __rdtsc();
  for(i=0;i<N;++i) {
    int size = (i % 64) + 1;
    ptrs[i] = realloc(ptrs[i], size*2);
  }
  end = __rdtsc();
  printf("realloc %lu\n", (end - start)/N);

  start = __rdtsc();
  for(i=0;i<N;++i) {
    free(ptrs[i]);
  }
  end = __rdtsc();
  printf("dealloc %lu\n", (end - start)/N);
}

int main(void) {
  mal_init(&allocator);

  printf("== warmup\n");
  bench_minialloc();
  bench_malloc();

  printf("== bench minialloc\n");
  bench_minialloc();

  printf("== bench malloc\n");
  bench_malloc();

  mal_destroy(&allocator);
  return 0;
}
