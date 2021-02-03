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

/* Number of pools, each pool is dedicated to a chunk in power of 2 size. */
#ifndef MAL_POOL_COUNT
#define MAL_POOL_COUNT 15
#endif

/* Max number of pages per pool. */
#ifndef MAL_PAGES_COUNT
#define MAL_PAGES_COUNT 32
#endif

/* Alignment for a allocations, must be at least 8. */
#ifndef MAL_ALLOC_ALIGN
#define MAL_ALLOC_ALIGN 16
#endif

/* Initial size for the first page in the pool. */
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
  union {
    mal_node* next; /* Pointer to the next free chunk in the pool. */
    unsigned int pool_index;
  };
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
MAL_API mal_result mal_add_pool(mal_allocator* allocator, size_t member_size, size_t member_count); /* Use to pre allocate a pool, member size must be in power of 2. */
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

#ifdef __GNUC__
  #define MAL_NO_INLINE __attribute__((noinline))
  #define MAL_LIKELY(x) __builtin_expect((x), 1)
  #define MAL_UNLIKELY(x) __builtin_expect((x), 0)
#else
  #define MAL_NO_INLINE
  #define MAL_LIKELY(x) (x)
  #define MAL_UNLIKELY(x) (x)
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

static inline mal_result _mal_is_power_of_two(size_t x) {
  return (x != 0) && ((x & (x - 1)) == 0);
}

static inline size_t _mal_align_forward(size_t addr, size_t align) {
  return (addr + (align-1)) & ~(align-1);
}

static inline size_t _mal_log2(size_t x) {
  size_t r = sizeof(void*)*8 - 1 - _mal_clz(x);
  return (1UL << r) == x ? r : r + 1;
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

static mal_result _mal_alloc_page(mal_pool* pool, size_t member_size, size_t member_count) {
  /* Check if we can add a new page. */
  if(pool->page_count >= MAL_PAGES_COUNT) {
    MAL_LOG("pool reached max number of pages");
    return MAL_OUT_OF_PAGES;
  }
  /* Allocate page buffer. */
  size_t chunk_size = _mal_align_forward(MAL_ALLOC_ALIGN + member_size, MAL_ALLOC_ALIGN);
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
  unsigned int pool_index = _mal_log2(member_size);
  if(pool_index >= MAL_POOL_COUNT) {
    MAL_LOG("pool cannot be found for this member size");
    return MAL_INVALID_ARGUMENTS;
  }
  mal_pool* pool = &allocator->pools[pool_index];
  return _mal_alloc_page(pool, member_size, member_count);
}

static MAL_NO_INLINE mal_result _mal_grow_pool(mal_pool* pool, unsigned int pool_index) {
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
  MAL_LOGF("pool growing, member_size=%d member_count=%d\n", (int)member_size, (int)member_count);
#endif
  return _mal_alloc_page(pool, member_size, member_count);
}

void* mal_alloc(mal_allocator* allocator, size_t size) {
  if(MAL_UNLIKELY(size == 0)) {
    return NULL;
  }
  unsigned int pool_index = _mal_log2(size);
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
  node->pool_index = pool_index;
  void* ptr = (void*)((size_t)node + MAL_ALLOC_ALIGN);
  return ptr;
fallback:
  /* Unable to allocate in the pool, fallback to standard allocator. */
  node = (mal_node*)MAL_MALLOC(size + MAL_ALLOC_ALIGN);
  node->pool_index = 0xffffffffU;
  ptr = (void*)((size_t)node + MAL_ALLOC_ALIGN);
  return ptr;
}

static inline void _mal_dealloc(mal_allocator* allocator, mal_node* node, unsigned int pool_index) {
  /* Add the new free node to the pool. */
  mal_pool* pool = &allocator->pools[pool_index];
  node->next = pool->head;
  pool->head = node;
}

void mal_dealloc(mal_allocator* allocator, void* ptr) {
  if(MAL_LIKELY(ptr != NULL)) { /* Only deallocate valid pointers. */
    mal_node* node = (mal_node*)((size_t)ptr - MAL_ALLOC_ALIGN);
    unsigned int pool_index = node->pool_index;
    if(MAL_LIKELY(pool_index != 0xffffffffU)) { /* The allocation is in the pool. */
      _mal_dealloc(allocator, node, pool_index);
    } else { /* The allocation is not in the pool. */
      MAL_FREE(node);
    }
  }
}

void* mal_realloc(mal_allocator* allocator, void* ptr, size_t size, size_t old_size) {
  if(MAL_LIKELY(ptr != NULL)) {
    mal_node* node = (mal_node*)((size_t)ptr - MAL_ALLOC_ALIGN);
    unsigned int pool_index = node->pool_index;
    if(MAL_LIKELY(pool_index != 0xffffffffU)) {
      if(MAL_LIKELY(size > 0)) {
        size_t member_size = 1 << pool_index;
        if(MAL_LIKELY(size > member_size)) { /* Growing, we need to allocate a more space. */
          void* newptr = mal_alloc(allocator, size);
          if(MAL_LIKELY(newptr != NULL)) { /* Allocation successful. */
            /* Copy the contents. */
            memcpy(newptr, ptr, old_size);
            /* Deallocate old node. */
            _mal_dealloc(allocator, node, pool_index);
            return newptr;
          }
          /* Allocation failed, just cancel the reallocation. */
          return NULL;
        }
        /* Shrinking, we can reuse the current allocation. */
        return ptr;
      } else { /* Deallocate when resizing to 0. */
        _mal_dealloc(allocator, node, pool_index);
        return NULL;
      }
    }
    /* Reallocation using the fallback allocator. */
    node = MAL_REALLOC(node, size + MAL_ALLOC_ALIGN);
    if(MAL_LIKELY(node != NULL)) {
      return (void*)((size_t)node + MAL_ALLOC_ALIGN);
    } else { /* Reallocation failed. */
      return NULL;
    }
  }
  return mal_alloc(allocator, size);
}

#endif
