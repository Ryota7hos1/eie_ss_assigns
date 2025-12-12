#ifndef FREELIST_H
#define FREELIST_H

#include <stddef.h>

typedef struct common_header {
    int size;                    
    struct common_header *next;     
} common_header_t;

// three separate free list heads for each arena
extern common_header_t *freelist_small;
extern common_header_t *freelist_med;
extern common_header_t *freelist_large;

void init_free_list_explicit(common_header_t **head, void *mem, size_t mem_size);

#endif
