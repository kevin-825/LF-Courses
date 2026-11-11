#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

// 1. Override the standard C library puts() function
int puts(const char *s) {
    // 2. Look up the address of the "real" original puts() function in standard libc
    int (*real_puts)(const char *) = dlsym(RTLD_NEXT, "puts");
    
    // 3. Perform our custom action (interposition / hijacking)
    real_puts("[LD_PRELOAD INTERCEPTED]");
    
    // 4. Pass the original argument to the real function so the program keeps working
    return real_puts(s);
}
