#define MINIALLOC_IMPL
#include "minialloc.h"

#include <stdio.h>
#include <assert.h>

int main(void) {
  mal_allocator allocator;
  mal_init(&allocator);

  { /* Test simple allocation and deallocation. */
    void* ptr = mal_alloc(&allocator, 1);
    assert(ptr != NULL);
    mal_dealloc(&allocator, ptr);
  }

  { /* Test deallocation on NULL. */
    mal_dealloc(&allocator, NULL);
    assert(mal_realloc(&allocator, NULL, 0, 0) == NULL);
  }

  { /* Test simple allocation and deallocation via realloc. */
    void* ptr = mal_realloc(&allocator, NULL, 1, 0);
    assert(ptr != NULL);
    mal_realloc(&allocator, ptr, 0, 0);
  }

  { /* Test growing reallocation. */
    void* ptr1 = mal_realloc(&allocator, NULL, 1, 0);
    void* ptr2 = mal_realloc(&allocator, ptr1, 2, 0);
    assert(ptr1 != ptr2);
    void* ptr3 = mal_realloc(&allocator, ptr2, 3, 0);
    assert(ptr2 != ptr3);
    void* ptr4 = mal_realloc(&allocator, ptr3, 4, 0);
    assert(ptr3 == ptr4);
    mal_realloc(&allocator, ptr4, 0, 0);
  }

  { /* Test reuse of pool node. */
    void* ptr1 = mal_alloc(&allocator, 1);
    assert(ptr1 != NULL);
    mal_dealloc(&allocator, ptr1);

    void* ptr2 = mal_alloc(&allocator, 1);
    assert(ptr1 == ptr2);
    mal_dealloc(&allocator, ptr2);
  }

  mal_destroy(&allocator);
  return 0;
}
