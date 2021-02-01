#define MINIALLOC_IMPL
#include "minialloc.h"

#include <stdio.h>
#include <assert.h>
#include <x86intrin.h>
mal_allocator allocator;

int main(void) {
  mal_init(&allocator);

  int i;
  unsigned long start = __rdtsc();
  for(i=0;i<1000000;++i) {
    int size = i % 8;
    void* ptr = mal_alloc(&allocator, size);
    ptr = mal_realloc(&allocator, ptr, size*2, size);
    ptr = mal_realloc(&allocator, ptr, size*4, size);
    mal_dealloc(&allocator, ptr);
  }
  unsigned long end = __rdtsc();
  printf("%lu\n", (end - start)/1000000);

  mal_destroy(&allocator);
  return 0;
}
