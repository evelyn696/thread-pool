// tl_pthread_arena.c
#define _GNU_SOURCE
#include "arena.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <limits.h>

// Alignment macro
#define ALIGN(x) (((x) + sizeof(max_align_t) - 1) & ~(sizeof(max_align_t) - 1))
#define GROWTH_FACTOR 2

typedef struct BlockHeader {
    size_t size;               // aligned user size
    int freed;                 // whether it's on freelist
    struct BlockHeader* next;  // next free block (valid if freed)
} BlockHeader;

typedef struct ThreadArena {
    char* buffer;
    size_t capacity;
    size_t offset;
    BlockHeader* freelist; // sorted by address for coalescing
} ThreadArena;

// pthread key and one-time init
static pthread_key_t arena_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void arena_destructor(void* ptr) {
    ThreadArena* a = (ThreadArena*)ptr;
    if (!a) return;
    free(a->buffer);
    free(a);
}

static void make_key() {
    pthread_key_create(&arena_key, arena_destructor);
}

// Fetch or create the current thread's arena
static ThreadArena* get_arena() {
    pthread_once(&key_once, make_key);
    ThreadArena* a = pthread_getspecific(arena_key);
    if (!a) {
        // default initial size: 1 MiB
        size_t initial = MIN_THREAD_STACK;
        a = (ThreadArena*)malloc(sizeof(ThreadArena));
        if (!a) return NULL;
        a->capacity = ALIGN(initial);
        a->offset = 0;
        a->freelist = NULL;
        a->buffer = (char*)malloc(a->capacity);
        if (!a->buffer) {
            free(a);
            return NULL;
        }
        pthread_setspecific(arena_key, a);
    }
    return a;
}

// Internal: expand buffer to at least required
static int arena_expand(ThreadArena* a, size_t required) {
    size_t new_cap = a->capacity;
    while (new_cap < required) {
        new_cap = new_cap ? new_cap * GROWTH_FACTOR : ALIGN(required);
        if (new_cap < required) new_cap = required;
    }
    char* new_buf = (char*)realloc(a->buffer, new_cap);
    if (!new_buf) return -1;
    a->buffer = new_buf;
    a->capacity = new_cap;
    return 0;
}

// Internal: insert freed block in address order and coalesce
static void freelist_insert_and_coalesce(ThreadArena* a, BlockHeader* block) {
    block->freed = 1;
    block->next = NULL;

    BlockHeader* prev = NULL;
    BlockHeader* curr = a->freelist;

    // find insertion point (sorted)
    while (curr && curr < block) {
        prev = curr;
        curr = curr->next;
    }

    if (prev)
        prev->next = block;
    else
        a->freelist = block;
    block->next = curr;

    // coalesce with next
    if (block->next && block->next->freed) {
        uintptr_t end_block = (uintptr_t)(block + 1) + block->size;
        uintptr_t start_next = (uintptr_t)block->next;
        if (end_block == start_next) {
            block->size += sizeof(BlockHeader) + block->next->size;
            block->next = block->next->next;
        }
    }

    // coalesce with prev
    if (prev && prev->freed) {
        uintptr_t end_prev = (uintptr_t)(prev + 1) + prev->size;
        uintptr_t start_block = (uintptr_t)block;
        if (end_prev == start_block) {
            prev->size += sizeof(BlockHeader) + block->size;
            prev->next = block->next;
        }
    }
}

// Public API

void* armalloc(size_t size) {
    ThreadArena* a = get_arena();
    if (!a) return NULL;
    size_t aligned = ALIGN(size);
    size_t total = aligned + sizeof(BlockHeader);

    // search freelist first (first-fit)
    BlockHeader** prevp = &a->freelist;
    BlockHeader* curr = a->freelist;
    while (curr) {
        if (curr->freed && curr->size >= aligned) {
            *prevp = curr->next; // unlink
            curr->freed = 0;
            curr->next = NULL;
            return (void*)(curr + 1);
        }
        prevp = &curr->next;
        curr = curr->next;
    }

    // allocate new from bump region
    if (a->offset + total > a->capacity) {
        if (arena_expand(a, a->offset + total) != 0) return NULL;
    }

    BlockHeader* header = (BlockHeader*)(a->buffer + a->offset);
    header->size = aligned;
    header->freed = 0;
    header->next = NULL;
    a->offset += total;
    return (void*)(header + 1);
}

void* arcalloc(size_t num, size_t size) {
	// Overflow check
	if (size != 0 && num > SIZE_MAX / size) {
		return NULL;
	}
	
	size_t total = num * size;
	void *ptr = armalloc(total);
	if (ptr) {
		memset(ptr, 0, total);
	}
	return ptr;
}

void* arrealloc(void* ptr, size_t new_size) {
    ThreadArena* a = get_arena();
    if (!a) return NULL;
    if (!ptr) return armalloc(new_size);
    BlockHeader* header = ((BlockHeader*)ptr) - 1;
    size_t aligned_new = ALIGN(new_size);
    if (aligned_new <= header->size) {
        header->size = aligned_new; // shrink in place
        return ptr;
    }

    void* new_ptr = armalloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, header->size);
    arfree(ptr);
    return new_ptr;
}

void arfree(void* ptr) {
    if (!ptr) return;
    ThreadArena* a = get_arena();
    if (!a) return;
    BlockHeader* header = ((BlockHeader*)ptr) - 1;
    if (header->freed) return; // already freed
    freelist_insert_and_coalesce(a, header);
}

void arreset(void) {
    ThreadArena* a = get_arena();
    if (!a) return;
    a->offset = 0;
    a->freelist = NULL;
}

// void ardestroy(void) {
//     ThreadArena* a = get_arena();
//     if (!a) return;
//     // remove from TLS so destructor won't double-free
//     pthread_setspecific(arena_key, NULL);
//     free(a->buffer);
//     free(a);
// }
