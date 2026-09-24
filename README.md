# Tail-Call Deduplication: Cross-Module Reproducibility Guide

This document provides a minimal, reproducible test case to verify that the `DeduBB` CodeGen pass and Propeller successfully identify and fold identical basic blocks across different modules.

## 1. The Test Case (Before & After Assembly)

Instead of looking at the C++ source, let's look directly at the compiled assembly before and after our cross-module tail-call deduplication pass is applied.

**Before Deduplication (Baseline):**
We have two identical blocks of logic residing in completely separate translation units (`test1.cpp` and `test2.cpp`). Because they are compiled separately, the standard compiler cannot deduplicate them.

```assembly
0000000000001780 <_Z17identical_block_1i>:
    1780:       8d 04 7f                lea    (%rdi,%rdi,2),%eax
    1783:       8d 04 87                lea    (%rdi,%rax,4),%eax
    1786:       83 c0 2a                add    $0x2a,%eax
    1789:       35 ef be ad de          xor    $0xdeadbeef,%eax
    178e:       83 c0 9c                add    $0xffffff9c,%eax
    1791:       c3                      ret

00000000000017a0 <_Z17identical_block_2i>:
    17a0:       8d 04 7f                lea    (%rdi,%rdi,2),%eax
    17a3:       8d 04 87                lea    (%rdi,%rax,4),%eax
    17a6:       83 c0 2a                add    $0x2a,%eax
    17a9:       35 ef be ad de          xor    $0xdeadbeef,%eax
    17ae:       83 c0 9c                add    $0xffffff9c,%eax
    17b1:       c3                      ret
```

**After DeduBB Cross-Module Deduplication:**
Our `DeduBB` CodeGen pass identifies the duplication using Propeller. It promotes the first block to a global `DeduBB.master` symbol. The second block is wiped out and replaced with a jump to that global symbol. When the ThinLTO linker resolves the jump, it seamlessly redirects it to the address of `identical_block_1`!

```assembly
0000000000001780 <_Z17identical_block_1i>:
    1780:       8d 04 7f                lea    (%rdi,%rdi,2),%eax
    1783:       8d 04 87                lea    (%rdi,%rax,4),%eax
    1786:       83 c0 2a                add    $0x2a,%eax
    1789:       35 ef be ad de          xor    $0xdeadbeef,%eax
    178e:       83 c0 9c                add    $0xffffff9c,%eax
    1791:       c3                      ret

0000000000001798 <_Z17identical_block_2i>:
    1798:       e9 e3 ff ff ff          jmp    1780 <_Z17identical_block_1i>
```

---

## 2. Step-by-Step Commands

Run the following commands from your `tail-call` root directory to reproduce the results above.

### Step 1: Compile with BBAddrMap
First, compile the two files into a single binary. We compile with ThinLTO (`-flto=thin`) and pass `-Wl,--lto-basic-block-address-map` to ensure the LLD linker correctly preserves the map.

```bash
./llvm-project/trunk_build/bin/clang++ -g -O2 -flto=thin -fbasic-block-address-map -fuse-ld=lld -Wl,--lto-basic-block-address-map test1.cpp test2.cpp -o test_lto_labels
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
./llvm-project/trunk_build/bin/clang++ -g -O2 -flto=thin -fbasic-block-address-map \
    -fuse-ld=lld -Wl,--lto-basic-block-address-map \
    -Wl,-mllvm,-dedubb-directives=dedubb_directives.txt \
    test1.cpp test2.cpp -o test_deduplicated
```

### Step 4: Verify the Fold
Finally, disassemble the resulting binary to verify the fold matches the "After" assembly block above!

```bash
objdump -d test_deduplicated | grep -A 15 "<_Z17identical_block_"
```
