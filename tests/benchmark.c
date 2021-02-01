#define MINIALLOC_IMPL
#include "minialloc.h"

#include <stdio.h>
#include <assert.h>
#include <x86intrin.h>
mal_allocator allocator;

#define N 10000000
void* ptrs[N];

void bench() {
  int i;
  unsigned long start, end;

  start = __rdtsc();
  for(i=0;i<N;++i) {
    int size = i % 16;
    ptrs[i] = mal_alloc(&allocator, size);
  }
  end = __rdtsc();
  printf("alloc %lu\n", (end - start)/N);

  start = __rdtsc();
  for(i=0;i<N;++i) {
    int size = i % 16;
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

int main(void) {
  mal_init(&allocator);

  printf("== warmup\n");
  bench();
  printf("== bench 1\n");
  bench();
  printf("== bench 2\n");
  bench();

  mal_destroy(&allocator);
  return 0;
}
