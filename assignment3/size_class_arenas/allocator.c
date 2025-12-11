#include "allocator.h"
#include "freelist.h"

#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h> // Ensure size_t is available


// Global definitions for configuration
// Defaulting to BEST_FIT (2) and Merge Enabled (1) for Arena testing
int FIT_STRATEGY = BEST_FIT; 
int MERGE_ENABLED = 1;


// mmap wrapper
void *get_mem_block(void *addr, size_t mem_size) {
    void *p = mmap(addr, mem_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

size_t allocator_req_mem(size_t payload) {
    return payload + sizeof(common_header_t);
}

// --- Helper to get the correct freelist head based on request size ---
common_header_t **get_head_for_size(size_t total_req) {
    if (total_req <= TINY_MAX_SIZE) {
        return &tiny_freelist_head;
    } else if (total_req <= SMALL_MAX_SIZE) {
        return &small_freelist_head;
    } else { // Medium handles > 8 KB up to 32 KB (max request size)
        return &medium_freelist_head;
    }
}


// total free data allocation bytes (UPDATED for Arenas)
size_t allocator_free_mem_size(void) {
    size_t sum = 0;
    common_header_t *heads[] = {tiny_freelist_head, small_freelist_head, medium_freelist_head};

    for (int i = 0; i < 3; ++i) {
        for (common_header_t *c = heads[i]; c; c = c->next) {
            sum += (size_t)c->size;
        }
    }
    return sum;
}

// print free list (UPDATED for Arenas)
void allocator_list_dump(void) {
    common_header_t *heads[] = {tiny_freelist_head, small_freelist_head, medium_freelist_head};
    const char *names[] = {"TINY", "SMALL", "MEDIUM"};
    
    for (int i = 0; i < 3; ++i) {
        printf("\n%s List: ", names[i]);
        common_header_t *c = heads[i];
        int first = 1;
        while (c) {
            if (!first) printf(" -> ");

            printf("[%d]", c->size);
            first = 0;
            c = c->next;
        }
        printf("\n");
    }
}


// --- New Initialization Logic ---
static int init_arenas() {
    // 1. Tiny Arena
    tiny_base_addr = get_mem_block(NULL, TINY_ARENA_SIZE);
    if (!tiny_base_addr) return 0;
    init_free_list(tiny_base_addr, TINY_ARENA_SIZE, &tiny_freelist_head);

    // 2. Small Arena
    small_base_addr = get_mem_block(NULL, SMALL_ARENA_SIZE);
    if (!small_base_addr) return 0;
    init_free_list(small_base_addr, SMALL_ARENA_SIZE, &small_freelist_head);

    // 3. Medium Arena
    medium_base_addr = get_mem_block(NULL, MEDIUM_ARENA_SIZE);
    if (!medium_base_addr) return 0;
    init_free_list(medium_base_addr, MEDIUM_ARENA_SIZE, &medium_freelist_head);

    return 1;
}


void *smalloc(size_t n) // Best-Fit with Arena Routing
{
    if (n == 0) return NULL;
    size_t total_req = allocator_req_mem(n);
    
    // Initialize ALL arenas on first call
    if (!tiny_base_addr) {
        if (!init_arenas()) return NULL;
    }
    
    // 1. ROUTE: Get the correct freelist head pointer
    common_header_t **freelist_head_ptr = get_head_for_size(total_req);
    // If request is larger than 32KB (max size of MEDIUM), it fails here (NULL ptr)
    if (freelist_head_ptr == NULL) return NULL; 
    
    common_header_t *freelist_head_current = *freelist_head_ptr;

    common_header_t *best = NULL;
    common_header_t *best_prev = NULL;
    common_header_t *prev = NULL;
    common_header_t *cur = freelist_head_current;
    
    // 2. BEST-FIT SEARCH (inside the selected arena only)
    while (cur != NULL) {
        if (cur->size >= (int)n) {
            
            // --- Fit Strategy Implementation ---
            if (FIT_STRATEGY == BEST_FIT) {
                // Best-Fit: Find the smallest suitable block
                if (best == NULL || cur->size < best->size) {
                    best = cur;
                    best_prev = prev;
                }
            } else if (FIT_STRATEGY == FIRST_FIT) {
                // First-Fit: Use the first suitable block found and stop searching
                best = cur;
                best_prev = prev;
                break; // Found it, exit the loop
            }
            // ------------------------------------

        }
        prev = cur;
        cur = cur->next;
    }

    if (best == NULL) return NULL; // No free block big enough in this arena

    // 3. SPLITTING AND REMOVAL (logic remains the same, but updates the selected head)
    int remainder = best->size - (int)n - (int)sizeof(common_header_t);

    if (remainder >= (int)(sizeof(common_header_t) + 1)) {
        uint8_t *base = (uint8_t*)best;
        common_header_t *new_block = (common_header_t*)(base + sizeof(common_header_t) + n);

        new_block->size = remainder;
        new_block->next = best->next;

        best->size = (int)n;

        // REMOVAL: Update the head pointer through the pointer-to-pointer
        if (best_prev == NULL)
            *freelist_head_ptr = new_block; // Update the selected arena's head
        else
            best_prev->next = new_block;
    } else {
        if (best_prev == NULL) {
            *freelist_head_ptr = best->next; // Update the selected arena's head
        } else {
            best_prev->next = best->next;
        }
    }

    return (uint8_t*)best + sizeof(common_header_t);
}


// The following helper functions must be updated to take the arena head as a parameter
static common_header_t *insert_sorted_and_return_prev(common_header_t *block, common_header_t **head_ptr) {
    common_header_t *freelist_head = *head_ptr; // Get the correct head for this arena
    
    // Check 1: Insertion at the head (before the current head)
    if (freelist_head == NULL || block < freelist_head) {
        block->next = freelist_head;
        *head_ptr = block; // <--- Correctly updates the head pointer (e.g., tiny_freelist_head)
        return NULL;
    }

    // Check 2: Insertion in the middle
    common_header_t *cur = freelist_head;
    while (cur->next != NULL && cur->next < block) {
        cur = cur->next;
    }

    block->next = cur->next;
    cur->next = block;
    return cur; // The physical predecessor
}

// try_merge_with_next logic (NO CHANGE)
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

// try_merge_prev_with_next logic (NO CHANGE)
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


void sfree(void *ptr) {
    if (ptr == NULL) return;

    common_header_t *block = (common_header_t*)((uint8_t*)ptr - sizeof(common_header_t));

    // 1. ROUTE: Determine which arena this block belongs to
    common_header_t **freelist_head_ptr = get_freelist_head_ptr(block);
    
    if (freelist_head_ptr == NULL) return; 

    // 2. INSERT: Insert and get the predecessor
    common_header_t *prev = insert_sorted_and_return_prev(block, freelist_head_ptr);

    // 3. MERGE: Coalesce within the determined arena
    if (MERGE_ENABLED) {
        // Try merging the newly inserted 'block' with the block immediately AFTER it in the list.
        try_merge_with_next(block); 

        if (prev != NULL) {
            // Try merging the block immediately BEFORE 'block' (i.e., 'prev') with the new 'block'.
            // Note: Since 'block' may have merged with its successor in the previous step, 
            // the new successor of 'prev' is either 'block' or the merged result.
            try_merge_prev_with_next(prev);
        }
    }
}


// Function required by allocation_stress_test.c to compute free list stats (UPDATED for Arenas)
void allocator_stats(size_t* N, size_t* F, size_t* L) {
    // This is the function required for the overall (combined) report
    size_t total_count = 0;
    size_t total_free_bytes = 0;
    size_t largest_block_size = 0;

    common_header_t *heads[] = {tiny_freelist_head, small_freelist_head, medium_freelist_head};

    for (int i = 0; i < 3; ++i) {
        common_header_t *current = heads[i];
        while (current != NULL) {
            total_count++;
            total_free_bytes += (size_t)current->size;

            // L must be the single largest block across *all* arenas for the final report.
            if ((size_t)current->size > largest_block_size) {
                largest_block_size = (size_t)current->size;
            }

            current = current->next;
        }
    }

    *N = total_count;
    *F = total_free_bytes;
    *L = largest_block_size;
}

// --- Specific Arena Stats for Fragmentation Report (NEW FUNCTION) ---
// Note: This is a helper function you'll need to call in your main/test file to get per-zone stats.
void allocator_arena_stats(int arena_index, size_t* N, size_t* F, size_t* L) {
    common_header_t *heads[] = {tiny_freelist_head, small_freelist_head, medium_freelist_head};
    common_header_t *current = heads[arena_index];
    
    size_t count = 0;
    size_t total_free_bytes = 0;
    size_t largest_block_size = 0;

    while (current != NULL) {
        count++;
        total_free_bytes += (size_t)current->size;

        if ((size_t)current->size > largest_block_size) {
            largest_block_size = (size_t)current->size;
        }

        current = current->next;
    }

    *N = count;
    *F = total_free_bytes;
    *L = largest_block_size;
}