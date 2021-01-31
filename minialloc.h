/*
Minimal amortized O(1) multi-pool allocator.
minialloc
Eduardo Bart - edub4rt@gmail.com
https://github.com/edubart/minialloc

*/

#ifndef MINIALLOC_H
#define MINIALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAL_POOL_COUNT
#define MAL_POOL_COUNT 16
#endif

#ifndef MAL_PAGES_COUNT
#define MAL_PAGES_COUNT 32
#endif

#ifndef MAL_INITIAL_POOL_SIZE
#define MAL_INITIAL_POOL_SIZE 1048576 /* 1MB */
#endif

/* Public API qualifier. */
#ifndef MAL_API
#define MAL_API extern
#endif

#include <stddef.h> /* For size_t. */

typedef enum mal_result {
  MAL_SUCCESS = 0,
  MAL_OUT_OF_PAGES,
  MAL_OUT_OF_MEMORY,
  MAL_INVALID_ARGUMENTS,
} mal_result;

typedef struct mal_node mal_node;
struct mal_node {
  mal_node* next;
};

typedef struct mal_node_tail {
  unsigned char offset;
  unsigned char pool_index;
} mal_node_tail;

typedef struct mal_page {
  unsigned char* buf;
  size_t size;
} mal_page;

typedef struct mal_pool {
  mal_node* head;
  size_t page_count;
  mal_page pages[MAL_PAGES_COUNT];
} mal_pool;

typedef struct mal_allocator {
  mal_pool pools[MAL_POOL_COUNT];
} mal_allocator;

MAL_API void mal_init(mal_allocator* allocator);
MAL_API mal_result mal_add_pool(mal_allocator* allocator, size_t member_size, size_t member_count);
MAL_API void mal_destroy(mal_allocator* allocator);

MAL_API void* mal_alloc(mal_allocator* allocator, size_t size);
MAL_API void* mal_alloc0(mal_allocator* allocator, size_t size);
MAL_API void* mal_realloc(mal_allocator* allocator, void* ptr, size_t size);
MAL_API void mal_dealloc(mal_allocator* allocator, void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* MINIALLOC_H */

#ifdef MINIALLOC_IMPL

#if !defined(MAL_NO_DEBUG) && !defined(NDEBUG) && !defined(MAL_DEBUG)
  #define MAL_DEBUG
#endif

#ifndef MAL_LOG
  #ifdef MAL_DEBUG
    #include <stdio.h>
    #define MAL_LOG(s) puts(s)
  #else
    #define MAL_LOG(s)
  #endif
#endif

#ifndef MAL_ASSERT
  #ifdef MAL_DEBUG
    #include <assert.h>
    #define MAL_ASSERT(c) assert(c)
  #else
    #define MAL_ASSERT(c)
  #endif
#endif

#ifndef MAL_MALLOC
  #include <stdlib.h>
  #define MAL_MALLOC malloc
  #define MAL_FREE free
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define _mal_clz(x) __builtin_clzl(x)
#else
#include <stdint.h>
#if UINTPTR_MAX == 0xffffffffffffffff
static size_t _mal_clz(size_t v) {
  static const size_t MultiplyDeBruijnBitPosition[64] = {
    63,  0, 58,  1, 59, 47, 53,  2,
    60, 39, 48, 27, 54, 33, 42,  3,
    61, 51, 37, 40, 49, 18, 28, 20,
    55, 30, 34, 11, 43, 14, 22,  4,
    62, 57, 46, 52, 38, 26, 32, 41,
    50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16,  9, 12,
    44, 24, 15,  8, 23,  7,  6,  5,
  };
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  return 63 - MultiplyDeBruijnBitPosition[((size_t)((v - (v >> 1))*0x07EDD5E59A4E28C2ULL)) >> 58];
}
#elif UINTPTR_MAX == 0xffffffff
static size_t _mal_clz(size_t v) {
  static const int MultiplyDeBruijnBitPosition[32] = {
    0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
    8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
  };
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return 31 - MultiplyDeBruijnBitPosition[(size_t)(v * 0x07C4ACDDU) >> 27];
}
#endif /* UINTPTR_MAX */
#endif /* defined(__GNUC__) && (__GNUC__ >= 4) */

#include <string.h> /* For memset. */

static mal_result _mal_is_power_of_two(size_t x) {
  return (x != 0) && ((x & (x - 1)) == 0);
}

static size_t _mal_align_forward(size_t addr, size_t align) {
  return (addr + (align-1)) & ~(align-1);
}

static size_t _mal_log2(size_t x) {
  size_t r = sizeof(void*)*8 - 1 - _mal_clz(x);
  return (1UL << r) == x ? r : r + 1;
}

static size_t _mal_get_member_align(size_t member_size) {
  size_t member_align;
  if(member_size >= 16) {
    member_align = 16;
  } else if(member_size >= 8) {
    member_align = 8;
  } else if(member_size >= 4) {
    member_align = 4;
  } else if(member_size >= 2) {
    member_align = 2;
  } else {
    member_align = 1;
  }
  return member_align;
}

static size_t _mal_get_chunk_size(size_t member_size) {
  const size_t node_size = sizeof(mal_node) + sizeof(mal_node_tail);
  const size_t node_align = sizeof(void*);
  size_t member_align = member_size > 16 ? 16 : member_size;
  size_t chunk_align = node_align > member_align ? node_align : member_align;
  size_t member_offset = _mal_align_forward(node_size, member_align);
  size_t chunk_size = _mal_align_forward(member_offset + member_size, chunk_align);
  return chunk_size;
}

void mal_init(mal_allocator* allocator) {
  memset(allocator, 0, sizeof(mal_allocator));
}

void mal_destroy(mal_allocator* allocator) {
  size_t i, j;
  for(i=0;i<MAL_POOL_COUNT;++i) {
    mal_pool* pool = &allocator->pools[i];
    for(j=0;j<pool->page_count;++j) {
      mal_page* page = &pool->pages[j];
      free(page->buf);
    }
  }
  memset(allocator, 0, sizeof(mal_allocator));
}

mal_result _mal_alloc_page(mal_pool* pool, size_t member_size, size_t member_count) {
  /* Check if we can add a new page. */
  if(pool->page_count >= MAL_PAGES_COUNT) {
    MAL_LOG("pool reached max number of pages");
    return MAL_OUT_OF_PAGES;
  }
  /* Allocate page buffer. */
  size_t chunk_size = _mal_get_chunk_size(member_size);
  size_t size = chunk_size * member_count;
  unsigned char* buf = (unsigned char*)MAL_MALLOC(size);
  if(!buf) {
    MAL_LOG("out of memory while allocating a pool page");
    return MAL_OUT_OF_MEMORY;
  }
  /* Link all free nodes in reverse order. */
  mal_node* head = NULL;
  size_t off = size;
  do {
    off = off - chunk_size;
    mal_node* node = (mal_node*)(&buf[off]);
    node->next = head;
    head = node;
  } while(off > 0);
  /* Add the new page. */
  mal_page* page = &pool->pages[pool->page_count];
  page->buf = buf;
  page->size = size;
  pool->page_count = pool->page_count + 1;
  /* Link page to the pool. */
  if(pool->head != NULL) { /* Not the first page. */
    assert(0);
  } else { /* First page. */
    pool->head = head;
  }
  return MAL_SUCCESS;
}

mal_result mal_add_pool(mal_allocator* allocator, size_t member_size, size_t member_count) {
  if(_mal_is_power_of_two(member_size) == MAL_SUCCESS) {
    MAL_LOG("pool member size must be a power of 2");
    return MAL_INVALID_ARGUMENTS;
  }
  if(member_count == 0) {
    MAL_LOG("pool member count cannot be 0");
    return MAL_INVALID_ARGUMENTS;
  }
  int pool_index = _mal_log2(member_size);
  if(pool_index >= MAL_POOL_COUNT) {
    MAL_LOG("pool cannot be found for this member size");
    return MAL_INVALID_ARGUMENTS;
  }
  mal_pool* pool = &allocator->pools[pool_index];
  _mal_alloc_page(pool, member_size, member_count);
  return MAL_SUCCESS;
}

static void* _mal_to_fallback_ptr(void* ptr) {
  return (void*)((size_t)ptr - 16);
}

static void* _mal_from_fallback_ptr(void* ptr) {
  return (void*)((size_t)ptr + 16);
}

void* mal_alloc(mal_allocator* allocator, size_t size) {
  if(size == 0) {
    return NULL;
  }
  int pool_index = _mal_log2(size);
  if(pool_index >= MAL_POOL_COUNT) { /* Allocation too large to fit any pool. */
    goto fallback;
  }
  mal_pool* pool = &allocator->pools[pool_index];
  mal_node* node = pool->head;
  if(node == NULL) { /* Initialize the pool. */
    size_t member_size = 1 << pool_index;
    if(_mal_alloc_page(pool, member_size, MAL_INITIAL_POOL_SIZE / member_size) != MAL_SUCCESS) {
      goto fallback; /* Out of pool memory. */
    }
    node = pool->head;
  }
  MAL_ASSERT(node);
  pool->head = node->next;
#ifdef MAL_DEBUG
  node->next = NULL;
#endif
  void* ptr = (void*)_mal_align_forward((size_t)node + sizeof(mal_node) + sizeof(mal_node_tail), _mal_get_member_align(size));
  mal_node_tail* tail = (mal_node_tail*)((size_t)ptr - sizeof(mal_node_tail));
  tail->offset = (size_t)ptr - (size_t)node;
  tail->pool_index = pool_index;
  return ptr;
fallback:
  ptr = malloc(size + 16);
  memset(ptr, 0, 16);
  return _mal_from_fallback_ptr(ptr);
}

void* mal_alloc0(mal_allocator* allocator, size_t size) {
  void* ptr = mal_alloc(allocator, size);
  if(ptr != NULL) {
    memset(ptr, 0, size);
  }
  return ptr;
}

static mal_node_tail* _mal_get_node_tail(void* ptr) {
  mal_node_tail* tail = (mal_node_tail*)((size_t)ptr - sizeof(mal_node_tail));
  if(tail->offset == 0) { /* Not allocated by the pool. */
    return NULL;
  }
  MAL_ASSERT(tail->offset < 16 && tail->pool_index < MAL_POOL_COUNT);
  return tail;
}

static void _mal_dealloc(mal_allocator* allocator, void* ptr, mal_node_tail* tail) {
  /* Get the pool. */
  mal_pool* pool = &allocator->pools[tail->pool_index];
  /* Get the node. */
  mal_node* node = (mal_node*)((size_t)ptr - tail->offset);
  MAL_ASSERT(node->next == NULL);
  /* Add the new free node to the pool. */
  node->next = pool->head;
  pool->head = node;
}

void mal_dealloc(mal_allocator* allocator, void* ptr) {
  mal_node_tail* tail = _mal_get_node_tail(ptr);
  if(tail) {
    _mal_dealloc(allocator, ptr, tail);
  } else { /* Allocation using the fallback allocator. */
    free(_mal_to_fallback_ptr(ptr));
  }
}

void* mal_realloc(mal_allocator* allocator, void* ptr, size_t size) {
  if(!ptr) {
    return mal_alloc(allocator, size);
  } else if(size == 0) {
    mal_dealloc(allocator, ptr);
    return NULL;
  }
  mal_node_tail* tail = _mal_get_node_tail(ptr);
  if(tail) {
    size_t member_size = 1 << tail->pool_index;
    if(size <= member_size) { /* Shrinking, just reuse current allocation. */
      return ptr;
    }
    /* Allocate a new pointer that can fit the space. */
    void* newptr = mal_alloc(allocator, size);
    /* Copy the contents. */
    memcpy(newptr, ptr, member_size);
    /* Deallocate old node. */
    _mal_dealloc(allocator, ptr, tail);
    return newptr;
  }
  /* Reallocation using the fallback allocator. */
  void* allocptr = _mal_to_fallback_ptr(ptr);
  void* newallocptr = realloc(allocptr, size + 16);
  if(allocptr == newallocptr) { /* Allocation reused. */
    return ptr;
  } else {
    return _mal_from_fallback_ptr(newallocptr);
  }
}

#endif
