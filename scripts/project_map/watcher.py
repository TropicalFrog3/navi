"""
File system watcher for live project map regeneration.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path
from typing import Callable, Dict

from .models import ParseConfig
from .scanner import scan_project_files


def watch_and_regenerate(
    config: ParseConfig,
    generate_fn: Callable[[ParseConfig], None],
    poll_interval: float = 1.0,
) -> None:
    """
    Watches project files and triggers `generate_fn` upon file modifications.
    """
    print(f"[*] Watching '{config.root_path}' for changes... (Press Ctrl+C to stop)")

    def get_file_mtimes() -> Dict[str, float]:
        mtimes: Dict[str, float] = {}
        files = scan_project_files(config)
        for full_path, rel_path in files:
            try:
                mtimes[rel_path] = os.path.getmtime(full_path)
            except OSError:
                pass
        return mtimes

    last_mtimes = get_file_mtimes()

    try:
        while True:
            time.sleep(poll_interval)
            current_mtimes = get_file_mtimes()

            changed = False
            if set(current_mtimes.keys()) != set(last_mtimes.keys()):
                changed = True
            else:
                for path, mtime in current_mtimes.items():
                    if path not in last_mtimes or mtime > last_mtimes[path]:
                        changed = True
                        break

            if changed:
                print("\n[!] File modification detected. Regenerating project map...")
                last_mtimes = current_mtimes
                try:
                    generate_fn(config)
                except Exception as e:
                    print(f"[-] Regeneration error: {e}", file=sys.stderr)

    except KeyboardInterrupt:
        print("\n[*] Stopped project map watcher.")
