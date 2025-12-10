#include "freelist.h"
#include <stddef.h>

common_header_t *freelist_head = NULL;

void init_free_list(void *mem, size_t mem_size) {
    freelist_head = (common_header_t*)mem;
    freelist_head->size = mem_size - sizeof(common_header_t);
    freelist_head->next = NULL;
}
