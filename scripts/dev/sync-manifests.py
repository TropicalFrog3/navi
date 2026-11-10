#!/usr/bin/env python3
"""
@file  sync-manifests.py
@brief Extract @brief from authored source file headers and sync CONTEXT.md manifests.

@notes
  - Scans src/, res/, tests/, tools/, and scripts/ for authored files.
  - Skips vendored, generated, and binary paths (e.g. protobufs, vendor libs).
  - Use --check-only in CI/pre-commit to verify 100% header coverage.

@see .agents/rules/file-header-standard.md
@see .agents/rules/manifest-sync.md
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# Workspaces and file discovery rules
WORKSPACES = {
    "src": {
        "context_file": REPO_ROOT / "src" / "CONTEXT.md",
        "base_dir": REPO_ROOT / "src",
        "extensions": {".cpp", ".hpp"},
        "exclude_patterns": [
            "src/core/version.hpp",            # generated
            "src/model/config_default.hpp",    # generated
            "src/model/sdk/protobufs",         # generated protobuf headers/code
        ],
    },
    "res": {
        "context_file": REPO_ROOT / "res" / "CONTEXT.md",
        "base_dir": REPO_ROOT / "res" / "inject-scripts",
        "extensions": {".js"},
        "exclude_patterns": [
            "res/inject-scripts/vendor",        # vendored (jszip)
        ],
    },
    "tests": {
        "context_file": REPO_ROOT / "tests" / "CONTEXT.md",
        "base_dir": REPO_ROOT / "tests",
        "extensions": {".py", ".cpp"},
        "exclude_patterns": [
            "__pycache__",
            ".pytest_cache",
        ],
    },
    "tools": {
        "context_file": REPO_ROOT / "tools" / "CONTEXT.md",
        "base_dir": REPO_ROOT / "tools",
        "extensions": {".py", ".cpp", ".cs"},
        "exclude_patterns": [
            "tools/steamstub-unpack/xkb_fix.c",
            "third_party",
            "bin",
            "obj",
        ],
    },
    "scripts": {
        "context_file": None,  # no CONTEXT.md for scripts yet
        "base_dir": REPO_ROOT / "scripts",
        "extensions": {".sh", ".py"},
        "exclude_patterns": [],
    },
}

BRIEF_REGEX = re.compile(r"@brief\s+([^\r\n*]+)")
FILE_REGEX = re.compile(r"@file\s+([^\r\n*]+)")


def extract_header_info(filepath: Path) -> tuple[str | None, str | None]:
    """Extract (@file, @brief) from the leading comment or docstring block."""
    try:
        content = filepath.read_text(encoding="utf-8", errors="replace")
    except Exception as exc:
        print(f"Warning: could not read {filepath}: {exc}", file=sys.stderr)
        return None, None

    # Only inspect the first 60 lines
    lines = content.splitlines()[:60]
    head = "\n".join(lines)

    file_match = FILE_REGEX.search(head)
    brief_match = BRIEF_REGEX.search(head)

    file_val = file_match.group(1).strip() if file_match else None
    brief_val = brief_match.group(1).strip() if brief_match else None

    return file_val, brief_val


def discover_authored_files(ws_config: dict) -> list[Path]:
    """Discover all non-excluded authored files under base_dir."""
    base_dir = ws_config["base_dir"]
    extensions = ws_config["extensions"]
    excludes = ws_config["exclude_patterns"]

    results: list[Path] = []
    if not base_dir.exists():
        return results

    for root, dirs, files in os.walk(base_dir):
        # Prune hidden dirs and ignored patterns
        dirs[:] = [d for d in dirs if not d.startswith(".") and d not in {"bin", "obj", "__pycache__", ".pytest_cache"}]
        for fname in files:
            fpath = Path(root) / fname
            # Skip symlinks
            if fpath.is_symlink():
                continue
            if fname == "__init__.py":
                continue
            if fpath.suffix not in extensions:
                continue

            rel_to_repo = str(fpath.relative_to(REPO_ROOT))
            if any(exc in rel_to_repo for exc in excludes):
                continue

            results.append(fpath)

    return sorted(results)


def build_manifest_table(files: list[Path], ws_base_dir: Path) -> str:
    """Build markdown table for file manifest."""
    rows = [
        "<!-- MANIFEST:BEGIN -->",
        "| File | Purpose |",
        "|---|---|",
    ]
    for fpath in files:
        _, brief = extract_header_info(fpath)
        purpose = brief if brief else "TODO: Add @brief description"
        # Relative to workspace base dir (e.g. src/ or tests/)
        rel_path = fpath.relative_to(ws_base_dir)
        rows.append(f"| `{rel_path}` | {purpose} |")
    rows.append("<!-- MANIFEST:END -->")
    return "\n".join(rows)


def update_context_file(context_path: Path, new_manifest: str) -> bool:
    """Insert or replace the MANIFEST block in context_path."""
    if not context_path.exists():
        return False

    content = context_path.read_text(encoding="utf-8")
    manifest_pattern = re.compile(r"<!-- MANIFEST:BEGIN -->.*?<!-- MANIFEST:END -->", re.DOTALL)

    if manifest_pattern.search(content):
        updated = manifest_pattern.sub(new_manifest, content)
    else:
        # Append as a new section
        updated = content.rstrip() + "\n\n## File Manifest\n\n" + new_manifest + "\n"

    if updated != content:
        context_path.write_text(updated, encoding="utf-8")
        return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync CONTEXT.md manifests with source file @brief headers")
    parser.add_argument("--check-only", action="store_true", help="Only verify all authored files have @brief headers")
    args = parser.parse_args()

    missing_headers: list[Path] = []
    all_files_by_ws: dict[str, list[Path]] = {}

    for ws_name, ws_config in WORKSPACES.items():
        files = discover_authored_files(ws_config)
        all_files_by_ws[ws_name] = files

        for fpath in files:
            _, brief = extract_header_info(fpath)
            if not brief:
                missing_headers.append(fpath)

    if missing_headers:
        print(f"Error: Found {len(missing_headers)} authored source file(s) missing '@brief':", file=sys.stderr)
        for p in missing_headers:
            print(f"  - {p.relative_to(REPO_ROOT)}", file=sys.stderr)
        if args.check_only:
            return 1

    if args.check_only:
        total_files = sum(len(f) for f in all_files_by_ws.values())
        print(f"OK: All {total_files} authored source files contain valid @brief headers.")
        return 0

    # Sync manifests to CONTEXT.md files
    updated_contexts: list[str] = []
    for ws_name, ws_config in WORKSPACES.items():
        ctx_file = ws_config.get("context_file")
        if not ctx_file or not ctx_file.exists():
            continue

        files = all_files_by_ws[ws_name]
        # For 'src', base is src; for 'res', base is res; for 'tests', base is tests; for 'tools', base is tools
        ws_root = REPO_ROOT / ws_name
        manifest_md = build_manifest_table(files, ws_root)
        changed = update_context_file(ctx_file, manifest_md)
        if changed:
            updated_contexts.append(str(ctx_file.relative_to(REPO_ROOT)))

    if updated_contexts:
        print(f"Successfully updated manifests in {len(updated_contexts)} CONTEXT.md file(s):")
        for c in updated_contexts:
            print(f"  - {c}")
    else:
        print("All CONTEXT.md manifests are up to date.")

    if missing_headers:
        print(f"\nNotice: {len(missing_headers)} files still need @brief headers. Re-run after adding headers.")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

