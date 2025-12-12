#include "allocator.h"
#include "freelist.h"

#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h> /* for memset */

/* global definitions that are: default to Best-Fit and Merging */
int FIT_STRATEGY = BEST_FIT;
int MERGE_ENABLED = 1;

/* Per-arena heap pointers (mmap'd regions) */
static void *heap_small = NULL;
static void *heap_med   = NULL;
static void *heap_large = NULL;

/* mmap wrapper */
void *get_mem_block(void *addr, size_t mem_size) {
    void *p = mmap(addr, mem_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

/* utility: requested memory includes header */
size_t allocator_req_mem(size_t payload) {
    return payload + sizeof(common_header_t);
}

/* total free data allocation bytes across all arenas */
size_t allocator_free_mem_size(void) {
    size_t sum = 0;
    common_header_t *c;
    for (c = freelist_small; c; c = c->next) sum += c->size;
    for (c = freelist_med;   c; c = c->next) sum += c->size;
    for (c = freelist_large; c; c = c->next) sum += c->size;
    return sum;
}

/* print all freelists */
void allocator_list_dump(void) {
    common_header_t *c;
    printf("Small: ");
    c = freelist_small;
    if (!c) printf("(empty)");
    while (c) { printf("[%d]", c->size); if (c->next) printf(" -> "); c = c->next; }
    printf("\n");

    printf("Med:   ");
    c = freelist_med;
    if (!c) printf("(empty)");
    while (c) { printf("[%d]", c->size); if (c->next) printf(" -> "); c = c->next; }
    printf("\n");

    printf("Large: ");
    c = freelist_large;
    if (!c) printf("(empty)");
    while (c) { printf("[%d]", c->size); if (c->next) printf(" -> "); c = c->next; }
    printf("\n");
}

/* Stats helper: collect from a single freelist head */
static void collect_from_head(common_header_t *head, size_t* N, size_t* F, size_t* L) {
    for (common_header_t *c = head; c; c = c->next) {
        (*N)++;
        (*F) += (size_t)c->size;
        if ((size_t)c->size > *L) *L = (size_t)c->size;
    }
}

/* aggregate stats across the three arenas */
void allocator_stats(size_t* N, size_t* F, size_t* L) {
    if (!N || !F || !L) return;
    *N = *F = *L = 0;
    collect_from_head(freelist_small, N, F, L);
    collect_from_head(freelist_med,   N, F, L);
    collect_from_head(freelist_large, N, F, L);
}

/* Ensure arenas are created and initialised */
void init_arenas(void) {
    if (!heap_small) {
        heap_small = get_mem_block(NULL, SMALL_HEAP);
        if (heap_small) init_free_list_explicit(&freelist_small, heap_small, SMALL_HEAP);
    }
    if (!heap_med) {
        heap_med = get_mem_block(NULL, MED_HEAP);
        if (heap_med) init_free_list_explicit(&freelist_med, heap_med, MED_HEAP);
    }
    if (!heap_large) {
        heap_large = get_mem_block(NULL, LARGE_HEAP);
        if (heap_large) init_free_list_explicit(&freelist_large, heap_large, LARGE_HEAP);
    }
}

/* Helper: choose arena head pointer by requested payload size */
static common_header_t **arena_head_for_size(size_t n) {
    if (n <= SMALL_MAX) return &freelist_small;
    else if (n <= MED_MAX)   return &freelist_med;
    else return &freelist_large;
}

/* Helper: determine arena head by pointer value (when freeing). Uses address ranges. */
static common_header_t **arena_head_for_ptr(void *ptr) {
    if (ptr == NULL) return &freelist_small; 
    uintptr_t p = (uintptr_t)ptr;

    if (heap_small) {
        uintptr_t start = (uintptr_t)heap_small;
        uintptr_t end = start + SMALL_HEAP;
        if (p >= start && p < end) return &freelist_small;
    }
    if (heap_med) {
        uintptr_t start = (uintptr_t)heap_med;
        uintptr_t end = start + MED_HEAP;
        if (p >= start && p < end) return &freelist_med;
    }
    // default
    return &freelist_large;
}

/* Insert sorted into a specified freelist; return previous node pointer (or NULL if inserted at head) */
static common_header_t *insert_sorted_and_return_prev(common_header_t *block, common_header_t **head) {
    if (head == NULL || block == NULL) return NULL;

    if (*head == NULL || block < *head) {
        block->next = *head;
        *head = block;
        return NULL; // no previous node
    }

    common_header_t *cur = *head;
    while (cur->next != NULL && cur->next < block) {
        cur = cur->next;
    }

    block->next = cur->next;
    cur->next = block;
    return cur;
}

/* Merge helpers operate only on the provided freelist nodes (no global) */
static int try_merge_with_next(common_header_t *block) {
    if (block == NULL || block->next == NULL) return 0;

    uint8_t *block_end = (uint8_t*)block + sizeof(common_header_t) + (size_t)block->size;
    if (block_end == (uint8_t*)block->next) { // adjacent: absorb next
        block->size += (int)(sizeof(common_header_t) + (size_t)block->next->size);
        block->next = block->next->next;
        return 1;
    }
    return 0;
}

static int try_merge_prev_with_next(common_header_t *prev) {
    if (prev == NULL || prev->next == NULL) return 0;

    uint8_t *prev_end = (uint8_t*)prev + sizeof(common_header_t) + (size_t)prev->size;
    if (prev_end == (uint8_t*)prev->next) {
        prev->size += (int)(sizeof(common_header_t) + (size_t)prev->next->size);
        prev->next = prev->next->next;
        return 1;
    }
    return 0;
}

/* smalloc: selects arena, finds best/first fit, splits/removes from that arena's freelist */
void *smalloc(size_t n) {
    if (n == 0) return NULL;

    /* Ensure arenas exist */
    init_arenas();

    /* Select arena freelist */
    common_header_t **arena_head = arena_head_for_size(n);
    if (arena_head == NULL) return NULL;

    /* Search freelist for best/first fit */
    common_header_t *best = NULL;
    common_header_t *best_prev = NULL;
    common_header_t *prev = NULL;
    common_header_t *cur = *arena_head;

    while (cur != NULL) {
        if ((size_t)cur->size >= n) {
            if (FIT_STRATEGY == BEST_FIT) {
                if (best == NULL || cur->size < best->size) {
                    best = cur;
                    best_prev = prev;
                }
            } else { /* FIRST_FIT */
                best = cur;
                best_prev = prev;
                break;
            }
        }
        prev = cur;
        cur = cur->next;
    }

    if (best == NULL) return NULL; /* no free block big enough */

    /* split condition variable */
    int remainder = best->size - (int)n - (int)sizeof(common_header_t);

    if (remainder >= (int)(sizeof(common_header_t) + 1)) {
        /* create new free block after allocated region */
        uint8_t *base = (uint8_t*)best;
        common_header_t *new_block = (common_header_t*)(base + sizeof(common_header_t) + n);
        new_block->size = remainder;
        new_block->next = best->next;

        best->size = (int)n;

        /* replace best in freelist with new_block */
        if (best_prev == NULL) {
            *arena_head = new_block;
        } else {
            best_prev->next = new_block;
        }
    } else {
        /* remove best from freelist */
        if (best_prev == NULL) {
            *arena_head = best->next;
        } else {
            best_prev->next = best->next;
        }
    }

    /* return pointer to usable payload area */
    return (uint8_t*)best + sizeof(common_header_t);
}

/* sfree: insert block back into the right arena freelist and merge if enabled */
void sfree(void *ptr) {
    if (ptr == NULL) return;

    /* compute header address */
    common_header_t *block = (common_header_t*)((uint8_t*)ptr - sizeof(common_header_t));

    /* find which arena this pointer belongs to */
    common_header_t **arena_head = arena_head_for_ptr(ptr);

    /* insert sorted into that freelist, receive prev node */
    common_header_t *prev = insert_sorted_and_return_prev(block, arena_head);

    if (MERGE_ENABLED) {
        /* try merge with next (block->next may have changed) */
        try_merge_with_next(block);

        /* if there is a previous node, try merging prev with its next */
        if (prev != NULL) {
            try_merge_prev_with_next(prev);
        }
    }
}
