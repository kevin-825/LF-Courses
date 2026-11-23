rm -f mem_lab
gcc -std=c11 -Wall -Wextra -O2 mem_lab.c -o mem_lab
strace -e trace=brk,mmap,munmap ./mem_lab
MALLOC_PERTURB_=90 ./mem_lab   # 90 = 0x5A -> newly allocated bytes become ~0x5A = 0xA5!