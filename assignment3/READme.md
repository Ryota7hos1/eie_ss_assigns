# 📘 Dynamic Memory Management – README

## Table of Contents
1. [Introduction](#introduction)  
2. [Top-Level Design](#top-level-design)  
   - [allocator.c](#allocatorc)  
   - [freelist.c](#freelistc)  
   - [allocator.h / freelist.h](#headers)  
   - [main.c](#mainc)  
3. [Test Version 1 – First-Fit Allocator](#test-version-1)  
4. [Test Version 2 – Best-Fit + Block Merging](#test-version-2)  
5. [Major Problems Encountered & Solutions](#major-problems-encountered-and-solutions)  
6. [Build & Run Instructions](#build-and-run)  
7. [Conclusion](#conclusion)


## Introduction

This project is a custom **dynamic memory allocator** implemented in C.  
It simulates a simplified version of `malloc()` and `free()` using a manually managed memory region obtained via `mmap()`.

The project was developed in versions, each adding more advanced allocator features:

- **Test Version 1:** Basic first-fit allocation, freeing of allocated memory and block splitting.
- **Test Version 2:** Best-fit allocation, inserting freed blocks in sorted order + adjacent block merging (coalescing).


## Top-Level Design

The system is built around several C modules, each with a clear responsibility.  
Together they form a minimal but functional memory management subsystem.

### `allocator.c`
This file implements the **allocation utilities**, including:
- `smalloc()` — allocates memory (first-fit in V1; best-fit in V2)
- `sfree()` — frees memory and merges adjacent blocks (V2)
- Memory statistics and debugging used in `main.c`:
    - `allocator_list_dump()`
    - `allocator_free_mem_size()`
    - `allocator_req_mem()`
- It also calls:
    - `get_mem_block()` - mmap wrapper to allocate a heap memory space
    - `freelist.h` - freelist definitions
    - `allocator.h` - allocator defintions

### `freelist.c`
Manages the internal **free list**, which stores unallocated memory blocks.  
Key responsibilities:
- Initialising the free list (`init_free_list`)
- Storing blocks as a linked list of `common_header_t`
- In Test Version 2: inserting blocks in **sorted order** and enabling **coalescing** of adjacent blocks.

### Headers (`allocator.h`, `freelist.h`)
These define:
- The memory layout structure (`common_header_t`)
- The public function prototypes
- The global free list pointer (`freelist_head`)
- `#define MEM_SIZE` for configurable simulated heap size

The headers enable clean separation of allocator logic and free list management.

### `main.c`
A test driver that:
- Runs a fixed logic pattern of allocations and frees that randomly change the memory space used
- Tracks successful and failed allocations  
- Prints the state of the allocator at the end  

`main.c` was used throughout development to validate correctness, quality of code and debugging.


## Test Version 1  
### First-Fit Allocation, Splitting, and Basic Freeing

In Test Version 1, the goal was to create a minimal working allocator that could allocate and deallocate memory correctly without memory leakage.

---

### First-Fit Allocation
`smalloc()` scanned the free list sequentially and chose the **first sufficiently large block**.

### Block Splitting
If a free block was larger than needed, it was split into:
- an allocated block
- a new smaller free block

This required careful pointer arithmetic to correctly map to the end of the allocated block:
```c
(base + sizeof(common_header_t) + n)
```

### Basic Free
`sfree()` simply added on the freed block to the head of the free list without the need of ordering or merging.


## Test Version 2  
### Best-Fit Allocation, Sorted Insert, and Merging (Coalescing)

Test Version 2 improved efficiency and reduced fragmentation through smarter block management by merging adjacent free blocks and allocating
memory in the nearest fitting space. Although this meant that the process would take longer because it would have to scan the entire free list, memory allocation was the obvious priority.

---

### Best-Fit Allocation

Instead of first-fit, the allocator searches the *entire* free list to find the block with the smallest leftover space that can satisfy the request. The cost is higher runtime, but the memory footprint becomes tighter.

### Sorted Insert of Freed Blocks

Freed blocks are inserted into the freelist **in ascending address order** rather than just dumping the node at the head each time.
This was necessary for reliable merging because two blocks can only be merged if they are:

1. consecutive in memory
2. consecutive in the freelist.

This ordering was implemented using a helper that would map to the free node before the block and reroute the pointers to insert the block into the correct address position:

```c
common_header_t* insert_sorted_and_return_prev(common_header_t *block);

```

## Major Problems Encountered and Solutions

### **Fix 1.** Allocator Infinite Loops
**Cause:** When allocating memory, incorrect pointer rewiring during block splitting created a mess. Specifically, we forgot to remove the newly allocated best-fit node from the free list.

**Fix:** Reconstructed the split logic and ensured allocated blocks were fully removed from the freelist after splitting:
```c
if (best_prev == NULL) {
            freelist_head = new_block;
        else {
            best_prev->next = new_block;
        }
}
```

### **Fix 2.** Incorrect Splitting Condition
**Cause:** When block splitting, hadn't considered the fact that we need to not only consider that there is extra unallocated space but also whether that space is large enough to store a free list node.

**Fix:** Added the size of the free list node to calculate whether we can allocate a free memory block:
```c
if (remainder >= (int)(sizeof(common_header_t) + 1)) 
```

### **Fix 3.** Incorrect offsetting Arithmetic
**Cause:** Initially had tried to use the block pointer `best` to be part of the offset address for when we split:
```c
 common_header_t *new_block = (common_header_t*)(best + sizeof(common_header_t) + n);
 ```

**Fix:** Needed to create a byte pointer `base` to be able to do the correct arithmetic:
```c
uint8_t *base = (uint8_t*)best;
common_header_t *new_block = (common_header_t*)(base + sizeof(common_header_t) + n);
```