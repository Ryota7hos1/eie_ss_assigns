# Run the main.c file along with the other c files 
gcc -Wall -Wextra -g allocator.c freelist.c main.c -o alloc_test

# Display the results of the test in the terminal output 
./alloc_test    