// main.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "allocator.h" // smalloc, sfree, allocator_list_dump, allocator_free_mem_size, allocator_req_mem

#define N_ALLOCS 45     // Make 45 allocation attempts at max
#define MAX_REQ_SIZE 16 // Allocate between 1..16 bytes in a request

// Set MEM_SIZE to 1024 in allocator.h

// Random allocation size between 1 and 16
static inline size_t rand_size() { return (size_t)(rand() % MAX_REQ_SIZE) + 1; }

int main(void)
{
    srand(time(0)); // Seed the RNG

    // Some basic stats to compute
    int num_succ_allocations = 0;
    int no_failure = 1;
    size_t failed_allocation_size = 0;
    size_t rem_free_mem = 0;

    // To store pointers to the last three allocations
    void *slots[3] = {0, 0, 0};

    // The main allocation loop
    for (int i = 0; i < N_ALLOCS && no_failure == 1; i++)
    {
        size_t req = rand_size();

        void *p = smalloc(req);

        if (p)
        {
            // The allocation was successful

            num_succ_allocations++;

            // Store in a 3-slot ring
            size_t ring = i % 3;

            slots[ring] = p;

            // Every 3rd allocation, free one of the last-3 active allocations

            if (((i + 1) % 3) == 0)
            {
                int r = rand() % 3;

                if (slots[r]) // If not already deallocated
                {
                    sfree(slots[r]);
                    slots[r] = NULL;
                }
            }
        }
        else
        {
            no_failure = 0;
            failed_allocation_size = req;
        }
    }

   
    printf("\nFree list: "); 

    allocator_list_dump(); // should print free list as: [size] -> [size] -> ...  (uses appropriate freelist function)

    rem_free_mem = allocator_free_mem_size(); // should return total free memory (uses appropriate freelist function)
    

    // Finally, print the report

    if (no_failure == 1)
    {
        printf("\nALLOCATION DONE WITH NO FAILURES\n");
    }
    else
    {
        printf("\nALLOCATION ATTEMPT FAILED AFTER %d ALLOCATIONS\n", num_succ_allocations);
    }

    printf("\nTotal Memory Size: %d bytes\n", MEM_SIZE);
    printf("\nMemory Used (data + headers): %zu bytes\n", MEM_SIZE - rem_free_mem);
    printf("\nRemaining Free Memory: %zu bytes\n", rem_free_mem);

    if (no_failure == 0)
    {
        // allocator_req_mem (below) returns: failed_allocation_size + size of the header (uses appropriate freelist function) 
        //as this is the total memory needed to be allocated
        printf("\nAllocation Required at Failure: %zu bytes\n\n", allocator_req_mem(failed_allocation_size));
    }

    return 0;
}
