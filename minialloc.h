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
#define MAL_POOL_COUNT 15
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
  mal_node* next; /* Pointer to the next free chunk in the pool. */
};

typedef struct mal_page {
  unsigned char* buf;
  size_t member_count;
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
MAL_API void* mal_realloc(mal_allocator* allocator, void* ptr, size_t size, size_t old_size);
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
    #define MAL_LOGF printf
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
  #define MAL_REALLOC realloc
  #define MAL_FREE free
#endif


#define MAL_NO_INLINE __attribute__((noinline))
#define MAL_LIKELY(x) __builtin_expect((x), 1)
#define MAL_UNLIKELY(x) __builtin_expect((x), 0)

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

typedef struct mal_node_tail {
  unsigned char offset; /* Offset between node pointer and member data. */
  unsigned char pool_index;
} mal_node_tail;

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

static void* _mal_to_fallback_ptr(void* ptr) {
  return (void*)((size_t)ptr - 16);
}

static void* _mal_from_fallback_ptr(void* ptr) {
  return (void*)((size_t)ptr + 16);
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
      MAL_FREE(page->buf);
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
  mal_node* head = pool->head;
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
  page->member_count = member_count;
  pool->page_count = pool->page_count + 1;
  /* Link page to the pool. */
  pool->head = head;
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
  return _mal_alloc_page(pool, member_size, member_count);
}

static MAL_NO_INLINE mal_result _mal_grow_pool(mal_pool* pool, int pool_index) {
  size_t member_size = 1 << pool_index;
  size_t member_count;
  if(pool->page_count > 0) { /* Double the size from last page. */
    member_count = pool->pages[pool->page_count - 1].member_count * 2;
  } else { /* Use default initial value. */
    member_count = MAL_INITIAL_POOL_SIZE / member_size;
    if(member_count == 0)
      member_count = 1;
  }
#ifdef MAL_DEBUG
  MAL_LOGF("pool growing, member_size=%lu member_count=%lu\n", member_size, member_count);
#endif
  return _mal_alloc_page(pool, member_size, member_count);
}

void* mal_alloc(mal_allocator* allocator, size_t size) {
  if(MAL_UNLIKELY(size == 0)) {
    return NULL;
  }
  int pool_index = _mal_log2(size);
  if(MAL_UNLIKELY(pool_index >= MAL_POOL_COUNT)) { /* Allocation too large to fit any pool. */
    goto fallback;
  }
  mal_pool* pool = &allocator->pools[pool_index];
  mal_node* node = pool->head;
  if(MAL_UNLIKELY(node == NULL)) { /* Initialize the pool. */
    if(MAL_UNLIKELY(_mal_grow_pool(pool, pool_index) != MAL_SUCCESS)) {
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
  MAL_ASSERT(tail->offset <= 16);
  tail->pool_index = pool_index;
  return ptr;
fallback:
  /* Unable to allocate in the pool, fallback to standard allocator. */
  ptr = MAL_MALLOC(size + 16);
  memset(ptr, 0, 16);
  return _mal_from_fallback_ptr(ptr);
}

static mal_node_tail* _mal_get_node_tail(void* ptr) {
  mal_node_tail* tail = (mal_node_tail*)((size_t)ptr - sizeof(mal_node_tail));
  if(MAL_LIKELY(tail->offset != 0)) { /* Allocated by the pool. */
    MAL_ASSERT(tail->offset <= 16 && tail->pool_index < MAL_POOL_COUNT);
    return tail;
  }
  return NULL; /* Not allocated by a pool. */
}

static void _mal_dealloc(mal_allocator* allocator, void* ptr, mal_node_tail* tail) {
  /* Get the node. */
  mal_node* node = (mal_node*)((size_t)ptr - tail->offset);
#ifdef MAL_DEBUG
  /* Reset tail offset to catch double frees. */
  tail->offset = 0;
#endif
  /* Add the new free node to the pool. */
  mal_pool* pool = &allocator->pools[tail->pool_index];
  MAL_ASSERT(node->next == NULL);
  node->next = pool->head;
  pool->head = node;
}

void mal_dealloc(mal_allocator* allocator, void* ptr) {
  if(MAL_LIKELY(ptr != NULL)) { /* Only deallocate valid pointers. */
    mal_node_tail* tail = _mal_get_node_tail(ptr);
    if(MAL_LIKELY(tail != NULL)) { /* The allocation is in the pool. */
      _mal_dealloc(allocator, ptr, tail);
    } else { /* The allocation is not in the pool. */
      MAL_FREE(_mal_to_fallback_ptr(ptr));
    }
  }
}

void* mal_realloc(mal_allocator* allocator, void* ptr, size_t size, size_t old_size) {
  if(MAL_LIKELY(ptr != NULL)) {
    mal_node_tail* tail = _mal_get_node_tail(ptr);
    if(MAL_LIKELY(tail != NULL)) {
      if(MAL_LIKELY(size > 0)) {
        size_t member_size = 1 << tail->pool_index;
        if(MAL_LIKELY(size > member_size)) { /* Growing, we need to allocate a more space. */
          void* newptr = mal_alloc(allocator, size);
          if(MAL_LIKELY(newptr != NULL)) { /* Allocation successful. */
            /* Copy the contents. */
            memcpy(newptr, ptr, old_size);
            /* Deallocate old node. */
            _mal_dealloc(allocator, ptr, tail);
            return newptr;
          }
          /* Allocation failed, just cancel the reallocation. */
          return NULL;
        }
        /* Shrinking, we can reuse the current allocation. */
        return ptr;
      } else { /* Deallocate when resizing to 0. */
        _mal_dealloc(allocator, ptr, tail);
        return NULL;
      }
    }
    /* Reallocation using the fallback allocator. */
    void* allocptr = _mal_to_fallback_ptr(ptr);
    void* newallocptr = MAL_REALLOC(allocptr, size + 16);
    if(MAL_LIKELY(newallocptr != NULL)) {
      return _mal_from_fallback_ptr(newallocptr);
    } else { /* Reallocation failed. */
      return NULL;
    }
  }
  return mal_alloc(allocator, size);
}

#endif
