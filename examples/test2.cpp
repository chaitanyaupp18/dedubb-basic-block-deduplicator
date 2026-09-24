#include <stdio.h>

extern int identical_block_1(int x);

__attribute__((noinline)) int identical_block_2(int x) {
    int y = x * 13;
    y += 42;
    y ^= 0xdeadbeef;
    y -= 100;
    return y;
}

int main(int argc, char** argv) {
    printf("%d %d\n", identical_block_1(argc), identical_block_2(argc));
    return 0;
}
