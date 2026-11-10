#!/usr/bin/env python3
"""
@file  steamstub_elf_unpack.py
@brief Offline unpacker and decryptor for SteamStub DRM ELF binaries


SteamStub DRM Variant 3.0 x64 ELF Unpacker.
Decrypts AES-256-CBC encrypted .text segment, handles stolen code block,
strips PKCS#7 padding, and patches e_entry to real entrypoint.
"""

import sys
import struct
import os
from collections import namedtuple

try:
    from Crypto.Cipher import AES
    from elftools.elf.elffile import ELFFile
except ImportError:
    print("Error: pyelftools and pycryptodome are required.")
    print("Run: pip install pyelftools pycryptodome")
    sys.exit(1)


def unpack_steamstub_elf(input_path: str, output_path: str) -> bool:
    print(f"[*] Reading '{input_path}'...")
    with open(input_path, "rb") as f:
        e = ELFFile(f)
        bind_sec = e.get_section_by_name(".bind")
        if not bind_sec:
            print("[-] No .bind section found in ELF file.")
            return False

        bind_addr = bind_sec["sh_addr"]
        bind_entry = e.header["e_entry"] - bind_addr
        bind_data = bind_sec.data()

        # Header offset is located at bind_entry - 0xc0
        header_offset = bind_entry - 0xc0
        key = int.from_bytes(bind_data[header_offset:header_offset + 4], "little")

        hdr = bytearray()
        curr_off = header_offset
        while len(hdr) < 256:
            curr_off += 4
            val = int.from_bytes(bind_data[curr_off:curr_off + 4], "little")
            hdr.extend(int.to_bytes(val ^ key, 4, "little"))
            key = val

        sig = struct.unpack("<I", hdr[0x00:0x04])[0]
        if sig != 0xC0DEC0DE:
            print(f"[-] Invalid SteamStub signature: {hex(sig)} != 0xc0dec0de")
            return False

        image_base = struct.unpack("<Q", hdr[0x04:0x0c])[0]
        drm_entry = struct.unpack("<I", hdr[0x0c:0x10])[0]
        real_entrypoint_rva = struct.unpack("<Q", hdr[0x18:0x20])[0]
        real_entrypoint = image_base + real_entrypoint_rva if real_entrypoint_rva < image_base else real_entrypoint_rva
        code_size = struct.unpack("<I", hdr[0x40:0x44])[0]
        aes_key = bytes(hdr[0x44:0x64])
        aes_iv = bytes(hdr[0x64:0x74])
        code_stolen = bytes(hdr[0x74:0x84])

        print(f"[+] SteamStub Variant 3.0 x64 detected!")
        print(f"    - Image Base       : 0x{image_base:x}")
        print(f"    - DRM Entrypoint   : 0x{drm_entry:x}")
        print(f"    - Real Entrypoint  : 0x{real_entrypoint:x} (RVA 0x{real_entrypoint_rva:x})")
        print(f"    - Code Size        : 0x{code_size:x} ({code_size} bytes)")
        print(f"    - AES Key          : {aes_key.hex()}")

        # Reconstruct IV via AES-ECB
        cipher_ecb = AES.new(aes_key, AES.MODE_ECB)
        new_iv = cipher_ecb.decrypt(aes_iv)

        text_sec = e.get_section_by_name(".text")
        if not text_sec:
            print("[-] No .text section found.")
            return False

        text_offset = text_sec["sh_offset"]
        text_data = text_sec.data()

        # Decrypt prepended code_stolen + encrypted code block
        cipher_cbc = AES.new(aes_key, AES.MODE_CBC, new_iv)
        decrypted = cipher_cbc.decrypt(code_stolen + text_data[:code_size])
        plaintext = decrypted[:code_size]

        f.seek(0)
        full_data = bytearray(f.read())

        # Write decrypted code to .text offset
        full_data[text_offset:text_offset + code_size] = plaintext

        # Patch e_entry in ELF header (offset 0x18 for 64-bit ELF)
        struct.pack_into("<Q", full_data, 0x18, real_entrypoint)

        print(f"[*] Writing unpacked ELF binary to '{output_path}'...")
        with open(output_path, "wb") as out:
            out.write(full_data)

        os.chmod(output_path, 0o755)
        print(f"[+] Unpack completed successfully!")
        return True


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input_elf> <output_elf>")
        sys.exit(1)
    success = unpack_steamstub_elf(sys.argv[1], sys.argv[2])
    sys.exit(0 if success else 1)
