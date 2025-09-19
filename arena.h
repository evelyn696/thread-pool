// arena.h
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>


// Minimum stack size for threads.  Required for some rANS codecs
// that use over 2Mbytes of stack for encoder / decoder state
#define MIN_THREAD_STACK (5 * 1024 * 1024)


/**
 * Thread-local arena allocator (via pthread_key_t).
 *
 * Features:
 *  - Implicit per-thread arena (no manual passing)
 *  - Per-allocation metadata
 *  - free(ptr) with coalescing
 *  - realloc semantics
 *  - arena_reset to drop all allocations (retains buffer)
 *  - Automatic cleanup on thread exit
 *
 * Usage:
 *    void* p = arena_alloc(128);
 *    arena_free(p);
 *    p = arena_realloc(p, 256);
 *    arena_reset();           // optional between tasks
 *    // no need to explicitly destroy on thread exit, but you can call:
 *    arena_destroy();         // optional early teardown
 */
void* armalloc(size_t size);
void* arcalloc(size_t num, size_t size);
void* arrealloc(void* ptr, size_t new_size);
void arreset(void);
void arfree(void* ptr);
// void ardestroy(void); // explicit if desired (otherwise auto on thread exit)

#endif // ARENA_H
