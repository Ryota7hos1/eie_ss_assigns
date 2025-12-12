#ifndef FREELIST_H
#define FREELIST_H

#include <stddef.h>

typedef struct common_header {
    int size;                        // payload bytes
    struct common_header *next;      // next free block (only valid when free)
} common_header_t;

extern common_header_t *freelist_head;

void init_free_list(void *mem, size_t mem_size);

#endif
