#include "freelist.h"
#include <stddef.h>

// three independent freelist heads
common_header_t *freelist_small = NULL;
common_header_t *freelist_med   = NULL;
common_header_t *freelist_large = NULL;

// initialize any freelist head for a memory region 
void init_free_list_explicit(common_header_t **head, void *mem, size_t mem_size) {
    if (head == NULL || mem == NULL || mem_size <= sizeof(common_header_t)) return; // need these definitions to run
    *head = (common_header_t*)mem;
    (*head)->size = (int)(mem_size - sizeof(common_header_t));
    (*head)->next = NULL;
}

