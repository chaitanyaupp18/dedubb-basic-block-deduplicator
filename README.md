# Tail-Call Deduplication: Cross-Module Reproducibility Guide

This document provides a minimal, reproducible test case to verify that the `DeduBB` CodeGen pass and Propeller successfully identify and fold identical basic blocks across different modules.

## 1. The Test Case

The test case consists of two separate source files. Each file contains an identical function that performs some arbitrary math. 
By avoiding inline assembly, we ensure that LLVM can naturally see the `ret` instruction at the end of the block and correctly tag it with `has_return()` in the `BBAddrMap`. Furthermore, the arbitrary math ensures the block size exceeds Propeller's minimum patch size (5 bytes).

**`test1.cpp`**
```cpp
#include <stdio.h>

__attribute__((noinline)) int identical_block_1(int x) {
    int y = x * 13;
    y += 42;
    y ^= 0xdeadbeef;
    y -= 100;
    return y;
}
```

**`test2.cpp`**
```cpp
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
```

---

## 2. Step-by-Step Commands

Run the following commands from your `tail-call` root directory.

### Step 1: Compile with BBAddrMap
First, compile the two files into a single binary, instructing Clang to emit the `BBAddrMap` sections so Propeller can read the block boundaries. We compile without ThinLTO here to ensure the `.llvm_bb_addr_map` section is flawlessly emitted.

```bash
./llvm-project/trunk_build/bin/clang++ -g -O2 -fbasic-block-address-map test1.cpp test2.cpp -o test_no_lto_labels
```

### Step 2: Generate Propeller Directives
Run the offline Propeller analysis tool on the generated binary. Propeller will scan the `BBAddrMap`, compare the bytes of all candidate blocks ending in a return, and output the deduplication directives.

```bash
./llvm-propeller/build/propeller/generate_propeller_profiles \
    --binary=test_lto_labels \
    --tail_call_profile=dedubb_directives.txt
```
*(You should see an output indicating: `1 master group(s), 1 fold(s)`)*

### Step 3: Apply Deduplication
Re-compile the source files, this time passing the generated directives file to the LLVM backend. 
> [!IMPORTANT]
> You must include `-fbasic-block-address-map` here as well so the blocks are assigned the `BBID`s that the `DeduBB` pass expects to match against!

```bash
./llvm-project/trunk_build/bin/clang++ -g -O2 -fbasic-block-address-map \
    -mllvm -dedubb-directives=dedubb_directives.txt \
    test1.cpp test2.cpp -o test_deduplicated
```

### Step 4: Verify the Fold
Finally, disassemble the resulting binary to verify that `identical_block_2` in the second module was replaced by a cross-module jump to `identical_block_1`.

```bash
objdump -d test_deduplicated | grep -A 15 "<_Z17identical_block_"
```

**Expected Output:**
```assembly
0000000000001140 <_Z17identical_block_1i>:
    1140:       8d 04 7f                lea    (%rdi,%rdi,2),%eax
    1143:       8d 04 87                lea    (%rdi,%rax,4),%eax
    1146:       83 c0 2a                add    $0x2a,%eax
    1149:       35 ef be ad de          xor    $0xdeadbeef,%eax
    114e:       83 c0 9c                add    $0xffffff9c,%eax
    1151:       c3                      ret
    1152:       66 2e 0f 1f 84 00 00    cs nopw 0x0(%rax,%rax,1)
    1159:       00 00 00 
    115c:       0f 1f 40 00             nopl   0x0(%rax)

0000000000001160 <_Z17identical_block_2i>:
    1160:       e9 db ff ff ff          jmp    1140 <_Z17identical_block_1i>
```
