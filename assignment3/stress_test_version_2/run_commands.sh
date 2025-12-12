# Run the c_allocation_stress_test.c file along with the other c files 
gcc -O2 -Wall -Wextra allocator.c freelist.c c_allocation_stress_test.c -o stress_test

# Display the results of the test in the terminal output 
./stress_test
#  To include the runtime display
time ./stress_test

# Need to change the fit strategy and merge enable in cthe code itself
