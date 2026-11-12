#include <stdio.h>

struct state {
    int buffer[4];
    int is_authenticated;
} g_state = { .buffer = {0}, .is_authenticated = 0 };

void buggy_fill(int count) {
    for (int i = 0; i <= count; i++) { /* Off-by-one bug! */
        g_state.buffer[i] = 0xdeadbeef;
    }
}
int main(void) {
    buggy_fill(4);
    printf("auth = 0x%x\n", g_state.is_authenticated);
    return 0;
}
