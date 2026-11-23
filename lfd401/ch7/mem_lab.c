#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *small = malloc(1024);          /* Uses brk() heap */
    char *large = malloc(256 * 1024);    /* Uses mmap() (>= 128 KiB) */
    printf("first byte of uninit small = 0x%02x\n", (unsigned char)small[0]);
    memset(large, 0x42, 256 * 1024);
    free(large);
    free(small);
    return 0;
}