---
name: linux-elf-reversing
description: Cheatsheet for offline reverse engineering of Linux ELF shared objects (like steamui.so) using Python scripts and objdump, specifically for resolving GOT-relative strings, finding dead code, and offline signature scanning.
---

# Linux ELF Offline Reversing Cheatsheet

When you need to analyze `.so` files in `~/.local/share/Steam/ubuntu12_32/` but do not have heavy tools like IDA or Ghidra installed, you can use these Python scripts to replicate the behavior and analyze the binary.

## 1. Resolving GOT-Relative Strings
In Linux PIE binaries, strings in `.rodata` are often accessed relative to the Global Offset Table (GOT) base pointer (`ebx`). 

**Example Assembly:**
```asm
1a1795c: lea eax, [ebx - 0x1a16424]
```
If you know the GOT base (which you can find via `readelf -S steamui.so | grep .got.plt` and looking at the address), you can compute the string's offset.

**Python Script:**
```python
import os
SO_PATH = os.path.expanduser("~/.local/share/Steam/ubuntu12_32/steamui.so")
GOT_BASE = 0x2501204 # Update this from readelf

with open(SO_PATH, "rb") as f:
    data = f.read()

# E.g., for 'lea eax, [ebx - 0x1a16424]'
offset = 0x1a16424
addr = GOT_BASE - offset

# Read string until null terminator
end = data.find(b"\0", addr)
print(f"String at {hex(addr)}: {data[addr:end].decode(errors='replace')}")
```

## 2. Finding Direct `E8` Callers (Xrefs)
To see if a function is dead code or actually used, you can scan the `.text` section for relative call instructions (`E8 rel32`) pointing to the target.

**Python Script:**
```python
import os, struct
SO_PATH = os.path.expanduser("~/.local/share/Steam/ubuntu12_32/steamui.so")
TEXT_VADDR = 0xc8a000
TEXT_OFF = 0xc89000
TEXT_SIZE = 0x01795ba4
TARGET = 0x17c7160

with open(SO_PATH, "rb") as f:
    data = f.read()

text = data[TEXT_OFF:TEXT_OFF + TEXT_SIZE]

i = text.find(b"\xe8")
while i != -1 and i + 5 <= len(text):
    rel = struct.unpack_from("<i", text, i + 1)[0]
    dst = TEXT_VADDR + i + 5 + rel
    if dst == TARGET:
        caller = TEXT_VADDR + i
        print(f"Target called from: {hex(caller)}")
    i = text.find(b"\xe8", i + 1)
```

## 3. Disassembling and Finding Prologues
You can use `objdump` to view the instructions of a function. Look at the registers to determine the calling convention (`cdecl` vs `regparm`).

```bash
# Dump the target address with a length of 50 bytes (approx 15-20 instructions)
objdump -d -M intel --start-address=0x17c6890 --stop-address=0x17c68c2 ~/.local/share/Steam/ubuntu12_32/steamui.so
```

**Checking for `regparm(3)`**:
If a function begins by operating on `eax`, `edx`, or `ecx` (without having moved them from the stack first), it expects arguments in those registers. Example:
```asm
17eeaf0: 55             push ebp
17eeaf1: 89 e5          mov ebp,esp
17eeaf3: 57             push edi
17eeaf4: 56             push esi
17eeaf5: 53             push ebx
17eeaf6: 83 ec 1c       sub esp,0x1c
17eeaf9: 89 c7          mov edi,eax    <-- First arg (this) is in eax
17eeafb: 89 d3          mov ebx,edx    <-- Second arg is in edx
17eeafd: 89 4d e0       mov [ebp-0x20],ecx <-- Third arg is in ecx
```
This is a `regparm(3)` function, meaning your hook typedef MUST be:
```cpp
typedef void(__attribute__((regparm(3))) *OriginalFn_t)(void* thisptr, void* arg2, void* arg3);
```
