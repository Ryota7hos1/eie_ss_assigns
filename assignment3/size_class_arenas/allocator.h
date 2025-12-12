#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include "freelist.h"   // defines common_header_t and extern freelist heads

#define MEM_SIZE (10*1024*1024)

// fit strategy and merge toggle 
#define FIRST_FIT 1
#define BEST_FIT 2

extern int FIT_STRATEGY;
extern int MERGE_ENABLED;

// size-class boundaries
#define SMALL_MAX   14*1024
#define MED_MAX     25*1024
// class memory capacity
#define SMALL_HEAP  (2*1024*1024)
#define MED_HEAP    (4*1024*1024)
#define LARGE_HEAP  (4*1024*1024)

// public allocator API
void *smalloc(size_t n);
void sfree(void *ptr);

void *get_mem_block(void *addr, size_t mem_size);

void init_arenas(void);

// utility functions used by the test 
size_t allocator_req_mem(size_t payload);
size_t allocator_free_mem_size(void);
void allocator_list_dump(void);

void allocator_stats(size_t* N, size_t* F, size_t* L);  // stress test

#endif
