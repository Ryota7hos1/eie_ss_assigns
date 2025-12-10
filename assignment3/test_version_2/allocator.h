
#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include "freelist.h"   // defines common_header_t and extern freelist_head

#define MEM_SIZE (1024)   

// Public allocator API
void *smalloc(size_t n);
void sfree(void *ptr);

// heap allocation wrapper
void *get_mem_block(void *addr, size_t mem_size);

// utility functions used by the test harness
size_t allocator_req_mem(size_t payload);
size_t allocator_free_mem_size(void);
void allocator_list_dump(void);

#endif
