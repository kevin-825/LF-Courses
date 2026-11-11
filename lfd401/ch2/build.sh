rm -rf *.so
gcc -shared -fPIC -Wall -Wextra -O2 hook_puts.c -o libhook.so -ldl
LD_PRELOAD=./libhook.so ls --version

# 1. Create a quick C test file that explicitly calls puts()
cat << 'EOF' > test.c
#include <stdio.h>

int main(void) {
    puts("Hello World!");
    return 0;
}
EOF

# 2. Compile it
gcc test.c -o test

# 3. Test with LD_PRELOAD
LD_PRELOAD=./libhook.so ./test


gcc -shared -fPIC -Wall -O2 hook_write.c -o libhook_write.so -ldl
LD_PRELOAD=./libhook_write.so whoami

