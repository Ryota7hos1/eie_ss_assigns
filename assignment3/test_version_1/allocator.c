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

// total free payload bytes
size_t allocator_free_mem_size(void) {
    size_t sum = 0;
    for (common_header_t *c = freelist_head; c; c = c->next)
        sum += c->size;
    return sum;
}

// print free list like this: [100] -> [50] -> [32]
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

void *smalloc(size_t n) {  // first fit
    if (n == 0) return NULL;

    // initialize memory and freelist if it hasn't been already
    if (!global_mem) {
        global_mem = get_mem_block(NULL, MEM_SIZE);
        if (!global_mem) return NULL;
        init_free_list(global_mem, MEM_SIZE);
    }

    common_header_t *prev = NULL;
    common_header_t *cur = freelist_head;

    while (cur) {
        if ((size_t)cur->size >= n) {  
            size_t remaining = cur->size - n;

            if (remaining > sizeof(common_header_t)) {  // split
                
                uint8_t *base = (uint8_t*)cur;  // base is needed for the arithmetic so we can do byte calculations
                common_header_t *new_block = (common_header_t*)(base + sizeof(common_header_t) + n);

                new_block->size = remaining - sizeof(common_header_t);
                new_block->next = cur->next;

                cur->size = n;

                if (prev) prev->next = new_block;
                else freelist_head = new_block;

            } 
            
            else { // allocate whole block if no remainder
                if (prev) prev->next = cur->next;
                else freelist_head = cur->next;
            }

            cur->next = NULL; // mark allocated
            return (uint8_t*)cur + sizeof(common_header_t);
        }

        prev = cur;
        cur = cur->next;
    }

    return NULL; // no block is big enough
}

void sfree(void *p) {
    if (!p) return;

    common_header_t *hdr = (common_header_t*)((uint8_t*)p - sizeof(common_header_t));

    hdr->next = freelist_head; // new space becomes head of freelist
    freelist_head = hdr;
}
