#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

#define MEM_SIZE (1024)  

void *smalloc(size_t n);
void sfree(void *p);

void *get_mem_block(void *addr, size_t mem_size);
size_t allocator_req_mem(size_t payload);
size_t allocator_free_mem_size(void);
void allocator_list_dump(void);

#endif
