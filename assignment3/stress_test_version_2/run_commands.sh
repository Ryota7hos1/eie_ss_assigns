# first fit, no merge
gcc -O2 -Wall -Wextra allocator.c freelist.c c_allocation_stress_test.c -o stress_test
./stress_test

# best fit, no merge


# best fit, merge
gcc -O2 -Wall -Wextra -FIT_STRATEGY=BEST_FIT -MERGE_ENABLED=1 allocator.c freelist.c c_allocation_stress_test.c -o stress_bestfit_merge1
./stress_bestfit_merge1
