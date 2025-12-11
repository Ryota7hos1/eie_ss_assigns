#include "freelist.h"
#include <stddef.h>
#include <stdint.h> // For size_t arithmetic

// Global definitions for the three arena heads
common_header_t *tiny_freelist_head = NULL;
common_header_t *small_freelist_head = NULL;
common_header_t *medium_freelist_head = NULL;

// Global definitions for the base addresses of each arena
void *tiny_base_addr = NULL;
void *small_base_addr = NULL;
void *medium_base_addr = NULL;


void init_free_list(void *mem, size_t mem_size, common_header_t **head_ptr) {
    *head_ptr = (common_header_t*)mem;
    (*head_ptr)->size = (int)(mem_size - sizeof(common_header_t));
    (*head_ptr)->next = NULL;
}

// FIX: This function now uses the consistent sizes from freelist.h
common_header_t **get_freelist_head_ptr(void *addr) {
    // Use char* for correct byte-wise pointer arithmetic
    char *c_addr = (char*)addr;
    
    // Determine the arena based on the address boundaries
    if (c_addr >= (char*)tiny_base_addr && c_addr < ((char*)tiny_base_addr + TINY_ARENA_SIZE)) {
        return &tiny_freelist_head;
    } 
    // IMPORTANT: Note that the boundaries must be mutually exclusive.
    if (c_addr >= (char*)small_base_addr && c_addr < ((char*)small_base_addr + SMALL_ARENA_SIZE)) {
        return &small_freelist_head;
    } 
    if (c_addr >= (char*)medium_base_addr && c_addr < ((char*)medium_base_addr + MEDIUM_ARENA_SIZE)) {
        return &medium_freelist_head;
    }
    
    return NULL; // Pointer is outside all known arenas
}