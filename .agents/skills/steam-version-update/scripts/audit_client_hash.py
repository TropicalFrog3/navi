#!/usr/bin/env python3
"""
audit_client_hash.py
====================
Inspects the local Steam client binary, calculates its SHA-256 hash,
and validates compliance against res/updates.yaml.
"""

import os
import sys
import hashlib
import re

def get_steamclient_path():
    candidates = [
        os.path.expanduser("~/.local/share/Steam/ubuntu12_32/steamclient.so"),
        os.path.expanduser("~/.steam/steam/ubuntu12_32/steamclient.so"),
        "/usr/lib/steam/ubuntu12_32/steamclient.so"
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None

def compute_sha256(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def extract_client_version(filepath):
    try:
        with open(filepath, "rb") as f:
            data = f.read()
            match = re.search(rb"version\((\d{10})\)", data)
            if match:
                return match.group(1).decode("ascii")
            match = re.search(rb"SteamClientVersion: (\d{10})", data)
            if match:
                return match.group(1).decode("ascii")
    except Exception:
        pass
    candidates_ver = [
        os.path.expanduser("~/.local/share/Steam/package/steam_client_ubuntu12.installed"),
        os.path.expanduser("~/.local/share/Steam/steam.sh")
    ]
    for cv in candidates_ver:
        if os.path.isfile(cv):
            try:
                with open(cv, "r", errors="ignore") as f:
                    txt = f.read()
                    m = re.search(r"(\d{10})", txt)
                    if m:
                        return m.group(1)
            except Exception:
                pass
    return "unknown"

def main():
    target = sys.argv[1] if len(sys.argv) > 1 else get_steamclient_path()
    if not target or not os.path.isfile(target):
        print(f"[ERROR] Could not find steamclient.so. Provide path as argument.", file=sys.stderr)
        sys.exit(1)

    client_hash = compute_sha256(target)
    client_version = extract_client_version(target)
    print(f"[INFO] Target: {target}")
    print(f"[INFO] Version: {client_version}")
    print(f"[INFO] SHA-256: {client_hash}")

    updates_yaml_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../res/updates.yaml"))
    if not os.path.isfile(updates_yaml_path):
        print(f"[WARN] updates.yaml not found at {updates_yaml_path}")
        return

    with open(updates_yaml_path, "r", encoding="utf-8") as f:
        content = f.read()

    hashes = re.findall(r"^\s*-\s+([0-9a-f]{64})", content, flags=re.MULTILINE)
    count = len(hashes)
    print(f"[INFO] updates.yaml total hashes: {count} (minimum required: 33)")
    if count < 33:
        print(f"[ERROR] Invariant violated! updates.yaml has fewer than 33 hashes ({count} < 33)!", file=sys.stderr)
        sys.exit(1)

    if client_hash in hashes:
        print(f"[PASS] Client hash {client_hash} is present in updates.yaml.")
    else:
        print(f"[ALERT] Client hash is NOT in updates.yaml!")
        print(f"Add the following line under the active release block in res/updates.yaml:")
        print(f"    - {client_hash} # steamclient - {client_version}")

if __name__ == "__main__":
    main()
