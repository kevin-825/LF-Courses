#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>

/* Intercept the low-level write system call wrapper */
ssize_t write(int fd, const void *buf, size_t count) {
    ssize_t (*real_write)(int, const void *, size_t) = dlsym(RTLD_NEXT, "write");
    
    /* Hook standard output (fd == 1) */
    if (fd == 1) {
        const char *prefix = "[WRITE INTERCEPTED] ";
        real_write(fd, prefix, strlen(prefix));
    }
    
    return real_write(fd, buf, count);
}
