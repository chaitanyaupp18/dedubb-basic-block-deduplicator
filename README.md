# Tail-Call Deduplication: Cross-Module Reproducibility Guide

This document provides a minimal, reproducible test case to verify that the `DeduBB` CodeGen pass and Propeller successfully identify and fold identical basic blocks across different modules.

## 1. The Test Case (Before & After Assembly)

Instead of looking at the C++ source, let's look directly at the compiled assembly before and after our cross-module tail-call deduplication pass is applied.

**Before Deduplication (Baseline):**
We have two identical blocks of logic residing in completely separate translation units (`test1.cpp` and `test2.cpp`). Because they are compiled separately, the standard compiler cannot deduplicate them.

```assembly
00000000000017c0 <_Z17identical_block_1i>:
    17c0:       53                      push   %rbx
    17c1:       89 fb                   mov    %edi,%ebx
    17c3:       81 ff 0f 27 00 00       cmp    $0x270f,%edi
    17c9:       75 0c                   jne    17d7 <_Z17identical_block_1i+0x17>
    17cb:       48 8d 3d 1a ee ff ff    lea    -0x11e6(%rip),%rdi
    17d2:       e8 a9 00 00 00          call   1880 <puts@plt>
    17d7:       8d 04 5b                lea    (%rbx,%rbx,2),%eax
    17da:       8d 04 83                lea    (%rbx,%rax,4),%eax
    17dd:       83 c0 2a                add    $0x2a,%eax
    17e0:       35 ef be ad de          xor    $0xdeadbeef,%eax
    17e5:       83 c0 9c                add    $0xffffff9c,%eax
    17e8:       5b                      pop    %rbx
    17e9:       c3                      ret

00000000000017f0 <_Z17identical_block_2i>:
    17f0:       53                      push   %rbx
    17f1:       89 fb                   mov    %edi,%ebx
    17f3:       81 ff 0f 27 00 00       cmp    $0x270f,%edi
    17f9:       75 0c                   jne    1807 <_Z17identical_block_2i+0x17>
    17fb:       48 8d 3d f0 ed ff ff    lea    -0x1210(%rip),%rdi
    1802:       e8 79 00 00 00          call   1880 <puts@plt>
    1807:       8d 04 5b                lea    (%rbx,%rbx,2),%eax
    180a:       8d 04 83                lea    (%rbx,%rax,4),%eax
    180d:       83 c0 2a                add    $0x2a,%eax
    1810:       35 ef be ad de          xor    $0xdeadbeef,%eax
    1815:       83 c0 9c                add    $0xffffff9c,%eax
    1818:       5b                      pop    %rbx
    1819:       c3                      ret
```

**After DeduBB Cross-Module Deduplication:**
Our `DeduBB` CodeGen pass identifies the duplication using Propeller. It promotes the first block to a global `DeduBB.master.0` symbol deep inside `identical_block_1`. The identical basic block inside `identical_block_2` is wiped out and replaced with a jump to that global symbol, which is seamlessly resolved by the ThinLTO linker!

```assembly
00000000000017c0 <_Z17identical_block_1i>:
    17c0:       53                      push   %rbx
    17c1:       89 fb                   mov    %edi,%ebx
    17c3:       81 ff 0f 27 00 00       cmp    $0x270f,%edi
    17c9:       75 0c                   jne    17d7 <DeduBB.master.0>
    17cb:       48 8d 3d 1a ee ff ff    lea    -0x11e6(%rip),%rdi
    17d2:       e8 a9 00 00 00          call   1880 <puts@plt>

00000000000017d7 <DeduBB.master.0>:
    17d7:       8d 04 5b                lea    (%rbx,%rbx,2),%eax
    17da:       8d 04 83                lea    (%rbx,%rax,4),%eax
    17dd:       83 c0 2a                add    $0x2a,%eax
    17e0:       35 ef be ad de          xor    $0xdeadbeef,%eax
    17e5:       83 c0 9c                add    $0xffffff9c,%eax
    17e8:       5b                      pop    %rbx
    17e9:       c3                      ret

00000000000017f0 <_Z17identical_block_2i>:
    17f0:       53                      push   %rbx
    17f1:       89 fb                   mov    %edi,%ebx
    17f3:       81 ff 0f 27 00 00       cmp    $0x270f,%edi
    17f9:       75 0c                   jne    1807 <_Z17identical_block_2i+0x17>
    17fb:       48 8d 3d f0 ed ff ff    lea    -0x1210(%rip),%rdi
    1802:       e8 79 00 00 00          call   1880 <puts@plt>
    1807:       e9 cb ff ff ff          jmp    17d7 <DeduBB.master.0>
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
objdump -d test_deduplicated | grep -A 25 "<_Z17identical_block_"
```
