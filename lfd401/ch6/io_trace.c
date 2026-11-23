#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("Hello ");
    printf("from ");
    printf("stdio!\n");          /* Flushes line-buffered stdout on \n */
    write(STDOUT_FILENO, "Raw syscall\n", 12);
    return 0;
}

