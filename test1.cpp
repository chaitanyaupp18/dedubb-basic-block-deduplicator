#include <stdio.h>

__attribute__((noinline)) int identical_block_1(int x) {
    int y = x * 13;
    y += 42;
    y ^= 0xdeadbeef;
    y -= 100;
    return y;
}
