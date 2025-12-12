#include "allocator.h"
#include "freelist.h"

#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>

static void *global_mem = NULL;

// mmap wrapper
void *get_mem_block(void *addr, size_t mem_size) {
    void *p = mmap(addr, mem_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

size_t allocator_req_mem(size_t payload) {
    return payload + sizeof(common_header_t);
}

// total free data allocation bytes
size_t allocator_free_mem_size(void) {
    size_t sum = 0;
    for (common_header_t *c = freelist_head; c; c = c->next) {
        sum += c->size;
    }
    return sum;
}

// print free list like this: [100] -> [46] -> [22]
void allocator_list_dump(void) {
    common_header_t *c = freelist_head;
    int first = 1;
    while (c) {
        if (!first) printf(" -> ");

        printf("[%d]", c->size);
        first = 0;
        c = c->next;
    }
    printf("\n");
}

void *smalloc(size_t n) // best fit
{
    if (n == 0) return NULL;

    if (!global_mem) {
        global_mem = get_mem_block(NULL, MEM_SIZE);
        if (!global_mem) return NULL;
        init_free_list(global_mem, MEM_SIZE);
    }

    common_header_t *best = NULL;
    common_header_t *best_prev = NULL;
    common_header_t *prev = NULL;
    common_header_t *cur = freelist_head;

    while (cur != NULL) {
        if (cur->size >= (int)n) {
            if (best == NULL || cur->size < best->size)  {// if first match or smaller than current best
                best = cur;
                best_prev = prev;
            }
        }
        prev = cur;
        cur = cur->next;
    }

    // no free block big enough
    if (best == NULL) return NULL;

    // split condition variable
    int remainder = best->size - (int)n - (int)sizeof(common_header_t);

    // split only if the leftover block is big enough to hold: the metadata block + at least 1 byte of data allocation
    if (remainder >= (int)(sizeof(common_header_t) + 1)) {
        uint8_t *base = (uint8_t*)best;

        // new free block starts after: best header + n allocated data bytes, of the base
        common_header_t *new_block = (common_header_t*)(base + sizeof(common_header_t) + n);

        new_block->size = remainder;
        new_block->next = best->next;

        best->size = (int)n;
        // remove best from free list
        if (best_prev == NULL)
            freelist_head = new_block;
        else
            best_prev->next = new_block;
        }

    else { // no remainder
        
        if (best_prev == NULL) {
            freelist_head = best->next;   // if best is at the start, change the head to the next one down
        }
        else {                            // otherwise make the previous one point past the newly allocated block and to the next freelist node
            best_prev->next = best->next; // that the best node was pointing to.
        }
    }

    // return pointer to the start of the data allocated block
    return (uint8_t*)best + sizeof(common_header_t);
}


static common_header_t *insert_sorted_and_return_prev(common_header_t *block) {  // static makes these private class to this file
    
    if (freelist_head == NULL || block < freelist_head) { // checks if there is no virtually previous freelist node
        block->next = freelist_head;
        freelist_head = block;
        return NULL; // no previous node
    }

    common_header_t *cur = freelist_head;
    while (cur->next != NULL && cur->next < block) {  // locates to just after the new free block
        cur = cur->next;
    }

    block->next = cur->next;
    cur->next = block;
    return cur; 
}

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


void sfree(void *ptr) {
    if (ptr == NULL) return;

    // compute the freed block's header address
    common_header_t *block = (common_header_t*)((uint8_t*)ptr - sizeof(common_header_t));

    // now this function is used to insert freed block into the linked list and return the previous node
    common_header_t *prev = insert_sorted_and_return_prev(block);

    try_merge_with_next(block);

    if (prev != NULL) {   // if the freelist node isn't the head node
        try_merge_prev_with_next(prev);
    }
}

