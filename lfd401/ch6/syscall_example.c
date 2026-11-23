#define _GNU_SOURCE
#include <sys/syscall.h>
#include <unistd.h>

#include <stdio.h>


int main(void) {
    /* Example: invoke getpid directly via SYS_getpid */
    pid_t pid = (pid_t)syscall(SYS_getpid);
    printf("Process ID: %ld\n", (long)pid);
}
