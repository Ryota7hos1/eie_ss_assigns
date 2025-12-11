#ifndef FREELIST_H
#define FREELIST_H

#include <stddef.h>

typedef struct common_header {
    int size;                        // payload bytes
    struct common_header *next;      // next free block (only valid when free)
} common_header_t;

// --- ARENA SIZES & BOUNDARIES ---
#define TINY_MAX_SIZE   256
#define SMALL_MAX_SIZE  (8 * 1024)  // 8 KB

#define TINY_ARENA_SIZE   (1 * 1024 * 1024) // 1 MB
#define SMALL_ARENA_SIZE  (3 * 1024 * 1024) // 3 MB
#define MEDIUM_ARENA_SIZE (6 * 1024 * 1024) // 6 MB
// --------------------------------

// Define the three independent free list heads
extern common_header_t *tiny_freelist_head;
extern common_header_t *small_freelist_head;
extern common_header_t *medium_freelist_head;

// Defines the base addresses for each arena (used for sfree routing)
extern void *tiny_base_addr;
extern void *small_base_addr;
extern void *medium_base_addr;

// Function to get the head pointer for a specific arena
common_header_t **get_freelist_head_ptr(void *addr);
// The simplified init function
void init_free_list(void *mem, size_t mem_size, common_header_t **head_ptr);

#endif