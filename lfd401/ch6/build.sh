
gcc -std=c11 -Wall -Wextra -O0 -ggdb3 syscall_example.c -o syscall_example


gcc -std=c11 -Wall -Wextra -O2 io_trace.c -o io_trace
ltrace -e printf,puts,fwrite,write ./io_trace
strace -e trace=write ./io_trace