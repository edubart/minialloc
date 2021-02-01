Experimental allocator using multiple pools for small allocations and falling back to `malloc` for large allocations.
Intended to be used in single threaded applications only.

The main benefit of this is allocator is that it improves allocation/deallocation
performance for small chunks, making them cost amortized O(1) time, also allows to
preallocate pools of sizes in power of two.

**NOTE: This project is not polished, but working as expected and is unlikely to be further developed.**
**NOTE: This was made as a toy experiment, there are better allocators that can do better such as rpmalloc.**
