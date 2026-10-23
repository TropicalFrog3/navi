"""
Directory scanner and file tree builder.
"""

from __future__ import annotations

import fnmatch
import os
from pathlib import Path
from typing import Dict, List, Set, Tuple

from .models import ParseConfig

DEFAULT_IGNORE_DIRS: Set[str] = {
    ".git",
    ".hg",
    ".svn",
    "__pycache__",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    ".cache",
    "node_modules",
    "bower_components",
    "venv",
    ".venv",
    "env",
    "build",
    "dist",
    "bin",
    "obj",
    "target",
    "out",
    ".vscode",
    ".idea",
    ".gemini",
    "third_party",
    "thirdparty",
    "vendor",
    "vendors",
    "external",
    "externals",
    "include",
    "lib",
}

DEFAULT_IGNORE_FILE_PATTERNS: Set[str] = {
    "*.o",
    "*.obj",
    "*.so",
    "*.a",
    "*.dylib",
    "*.dll",
    "*.exe",
    "*.pyc",
    "*.pyo",
    "*.pyd",
    "*.class",
    "*.jar",
    "*.war",
    "*.min.js",
    "*.min.css",
    "*.map",
    "*.pb.h",
    "*.pb.cc",
    "*.pb.cpp",
    "*.pb.c",
    "*_pb2.py",
    "*_pb2_grpc.py",
    "*.gen.*",
    "*.generated.*",
    "package-lock.json",
    "yarn.lock",
    "pnpm-lock.yaml",
    "flake.lock",
    "Cargo.lock",
    "*.png",
    "*.jpg",
    "*.jpeg",
    "*.gif",
    "*.ico",
    "*.svg",
    "*.webp",
    "*.pdf",
    "*.zip",
    "*.tar",
    "*.gz",
    "*.bin",
    "*.DS_Store",
}


def is_binary_file(file_path: Path) -> bool:
    """Check if file appears to be binary by inspecting first chunk."""
    try:
        with open(file_path, "rb") as f:
            chunk = f.read(1024)
            return b"\x00" in chunk
    except Exception:
        return True


def should_ignore_dir(dir_name: str, rel_path: str, config: ParseConfig) -> bool:
    """Determine if a directory should be skipped."""
    norm_rel = rel_path.replace(os.sep, "/")

    # If the user explicitly provided an include pattern matching this dir or subpaths, do not skip
    if config.include_patterns:
        for pat in config.include_patterns:
            clean_pat = pat.replace(os.sep, "/").rstrip("/*")
            if clean_pat == norm_rel or clean_pat.startswith(norm_rel + "/") or norm_rel.startswith(clean_pat + "/"):
                return False

    if dir_name in DEFAULT_IGNORE_DIRS or norm_rel in DEFAULT_IGNORE_DIRS:
        return True

    # Check depth
    if config.max_depth is not None:
        depth = len(Path(rel_path).parts)
        if depth > config.max_depth:
            return True

    # Check custom excludes against directory path
    for pattern in config.exclude_patterns:
        clean_pat = pattern.rstrip("/*")
        if fnmatch.fnmatch(norm_rel, clean_pat) or fnmatch.fnmatch(dir_name, clean_pat):
            return True

    return False


def matches_globs(path_str: str, patterns: List[str]) -> bool:
    """Check if normalized path matches any glob pattern."""
    norm = path_str.replace(os.sep, "/")
    for pat in patterns:
        norm_pat = pat.replace(os.sep, "/")
        if fnmatch.fnmatch(norm, norm_pat):
            return True
        # If pattern is a dir glob like src/**
        if norm_pat.endswith("/**"):
            prefix = norm_pat[:-3]
            if norm == prefix or norm.startswith(prefix + "/"):
                return True
        elif fnmatch.fnmatch(Path(norm).name, norm_pat):
            return True
    return False


def should_include_file(file_path: Path, rel_path: str, config: ParseConfig) -> bool:
    """Determine if a file should be included in analysis."""
    # Do not include the generated output file itself
    try:
        if file_path.resolve() == config.output_path.resolve():
            return False
    except Exception:
        pass

    file_name = file_path.name

    # Default file ignore patterns
    for pat in DEFAULT_IGNORE_FILE_PATTERNS:
        if fnmatch.fnmatch(file_name, pat):
            return False

    norm_rel = rel_path.replace(os.sep, "/")

    # Check depth
    if config.max_depth is not None:
        depth = len(Path(rel_path).parts) - 1
        if depth > config.max_depth:
            return False

    # Check custom excludes
    if config.exclude_patterns and matches_globs(norm_rel, config.exclude_patterns):
        return False

    # Check custom includes (if any specified, must match at least one)
    if config.include_patterns:
        if not matches_globs(norm_rel, config.include_patterns):
            return False

    # Check if binary
    if is_binary_file(file_path):
        return False

    return True


def scan_project_files(config: ParseConfig) -> List[Tuple[Path, str]]:
    """
    Recursively scans the root directory and returns a sorted list of
    (absolute_path, relative_path) for included source files.
    """
    root = config.root_path.resolve()
    included_files: List[Tuple[Path, str]] = []

    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        if rel_dir == ".":
            rel_dir = ""

        # Filter dirnames in-place to prune ignored directories
        dirnames[:] = [
            d for d in sorted(dirnames)
            if not should_ignore_dir(d, os.path.join(rel_dir, d) if rel_dir else d, config)
        ]

        for fname in sorted(filenames):
            rel_file = os.path.join(rel_dir, fname) if rel_dir else fname
            full_path = Path(dirpath) / fname

            if should_include_file(full_path, rel_file, config):
                included_files.append((full_path, rel_file.replace(os.sep, "/")))

    # Sort deterministically
    included_files.sort(key=lambda x: x[1])
    return included_files


def build_directory_tree(
    root_path: Path,
    included_files: List[Tuple[Path, str]],
    max_depth: Optional[int] = None,
) -> str:
    """
    Builds a deterministic text tree of the scanned directory structure.
    """
    # Build tree structure in memory
    tree: Dict[str, Any] = {}
    for _, rel_path in included_files:
        parts = rel_path.split("/")
        curr = tree
        for part in parts[:-1]:
            curr = curr.setdefault(part, {})
        curr[parts[-1]] = None

    lines = [f"{root_path.name}/"]

    def _render(node: Dict[str, Any], prefix: str = "", current_depth: int = 1):
        if max_depth is not None and current_depth > max_depth:
            return

        items = sorted(node.keys(), key=lambda s: (node[s] is None, s))
        count = len(items)

        for idx, key in enumerate(items):
            is_last = (idx == count - 1)
            connector = "└── " if is_last else "├── "
            child_prefix = "    " if is_last else "│   "

            if node[key] is None:
                lines.append(f"{prefix}{connector}{key}")
            else:
                lines.append(f"{prefix}{connector}{key}/")
                _render(node[key], prefix + child_prefix, current_depth + 1)

    _render(tree)
    return "\n".join(lines)
