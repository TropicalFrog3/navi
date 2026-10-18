#!/usr/bin/env python3
"""
audit_vftable.py
================
Audits virtual function tables (VFT) in 32-bit ELF binaries (steamclient.so, steamui.so)
even when symbols are stripped, by tracing Itanium C++ RTTI descriptors through .rel.dyn.
"""

import os
import sys
import struct

try:
    import elftools.elf.elffile as elffile
except ImportError:
    print("[ERROR] 'pyelftools' is required. Install via 'pip install pyelftools'.", file=sys.stderr)
    sys.exit(1)

def find_binary(name="steamclient.so"):
    candidates = [
        os.path.expanduser(f"~/.local/share/Steam/ubuntu12_32/{name}"),
        os.path.expanduser(f"~/.steam/steam/ubuntu12_32/{name}"),
        f"/usr/lib/steam/ubuntu12_32/{name}"
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None

def audit_class_vtable(binary_path, class_name, max_slots=95):
    with open(binary_path, "rb") as f:
        elf = elffile.ELFFile(f)

        def vaddr_to_offset(vaddr):
            for seg in elf.iter_segments():
                if seg['p_type'] == 'PT_LOAD':
                    if seg['p_vaddr'] <= vaddr < seg['p_vaddr'] + seg['p_filesz']:
                        return vaddr - seg['p_vaddr'] + seg['p_offset']
            return None

        rodata_sec = elf.get_section_by_name('.rodata')
        if not rodata_sec:
            print("[ERROR] .rodata section not found.", file=sys.stderr)
            return

        rodata = rodata_sec.data()
        rodata_vaddr = rodata_sec['sh_addr']

        # Format mangled RTTI name (e.g., "18CUserRemoteStorage")
        target_str = f"{len(class_name)}{class_name}\0".encode("ascii")
        str_idx = rodata.find(target_str)
        if str_idx == -1:
            # Fallback search for substring
            idx = 0
            while True:
                found = rodata.find(class_name.encode("ascii"), idx)
                if found == -1:
                    break
                # Check preceding length digit
                if found > 0 and rodata[found-1:found].isdigit():
                    str_idx = found - 1
                    while str_idx > 0 and rodata[str_idx-1:found].isdigit():
                        str_idx -= 1
                    break
                idx = found + 1

        if str_idx == -1:
            print(f"[WARN] RTTI type string for '{class_name}' not found in .rodata.")
            return

        str_vaddr = rodata_vaddr + str_idx
        print(f"[INFO] Found RTTI type name at vaddr: 0x{str_vaddr:08x}")

        rel_dyn = elf.get_section_by_name('.rel.dyn')
        if not rel_dyn:
            print("[ERROR] .rel.dyn section not found.", file=sys.stderr)
            return

        # Find relocation pointing to type name string
        typeinfo_vaddr = None
        for rel in rel_dyn.iter_relocations():
            off = vaddr_to_offset(rel['r_offset'])
            if off is None:
                continue
            f.seek(off)
            val = struct.unpack("<I", f.read(4))[0]
            if val == str_vaddr:
                typeinfo_vaddr = rel['r_offset'] - 4
                print(f"[INFO] Typeinfo descriptor at vaddr: 0x{typeinfo_vaddr:08x}")
                break

        if not typeinfo_vaddr:
            print(f"[WARN] Typeinfo descriptor pointing to 0x{str_vaddr:08x} not found.")
            return

        # Find vtables referencing this typeinfo
        vtables = []
        for rel in rel_dyn.iter_relocations():
            off = vaddr_to_offset(rel['r_offset'])
            if off is None:
                continue
            f.seek(off)
            val = struct.unpack("<I", f.read(4))[0]
            if val == typeinfo_vaddr:
                # Functions start right after typeinfo pointer
                vtables.append(rel['r_offset'] + 4)

        if not vtables:
            print(f"[WARN] No vtables found referencing typeinfo 0x{typeinfo_vaddr:08x}.")
            return

        for idx, vt_vaddr in enumerate(vtables):
            print(f"\n=======================================================")
            print(f" VTable #{idx+1} for {class_name} at 0x{vt_vaddr:08x}")
            print(f"=======================================================")
            for slot in range(max_slots):
                fn_vaddr_slot = vt_vaddr + slot * 4
                fn_off = vaddr_to_offset(fn_vaddr_slot)
                if fn_off is None:
                    break
                f.seek(fn_off)
                fn_addr = struct.unpack("<I", f.read(4))[0]
                if fn_addr < 0x00010000 or fn_addr > 0x03500000:
                    break

                note = ""
                if "RemoteStorage" in class_name:
                    known_names = {
                        74: "InternalGetSyncState (VFT 74)",
                        75: "GetConflictingFileTimestamps (VFT 75)",
                        76: "GetPendingRemoteOperationInfo (VFT 76)",
                        82: "ResetFileRequestState (VFT 82)"
                    }
                    if slot in known_names:
                        note = f" <== {known_names[slot]}"

                print(f"  [{slot:2d}] slot 0x{fn_vaddr_slot:08x} -> 0x{fn_addr:08x}{note}")

def main():
    target = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else find_binary("steamclient.so")
    class_name = sys.argv[2] if len(sys.argv) > 2 else "CUserRemoteStorage"

    if not target or not os.path.isfile(target):
        print("[ERROR] Binary not found. Usage: audit_vftable.py [binary_path] [ClassName]", file=sys.stderr)
        sys.exit(1)

    print(f"[INFO] Auditing {target} for class '{class_name}'...")
    audit_class_vtable(target, class_name)

if __name__ == "__main__":
    main()
