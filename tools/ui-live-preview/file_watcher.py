"""
File Watcher with Debounce for Navi Injected Scripts and UI Fixtures.

Zero external dependencies: uses threading and os.stat with debounce logic.
Detects creations, modifications, and deletions in watched directories.
"""

from __future__ import annotations

import os
from pathlib import Path
import threading
import time
from typing import Callable, Dict, List, Optional, Set


class FileWatcher:
    """
    Monitors directories for file modifications with configurable debouncing.
    """

    def __init__(
        self,
        watch_dirs: List[Path | str],
        extensions: Optional[Set[str]] = None,
        poll_interval: float = 0.25,
        debounce_delay: float = 0.20,
    ) -> None:
        self.watch_dirs = [Path(d).resolve() for d in watch_dirs]
        self.extensions = extensions or {".js", ".html", ".css", ".json"}
        self.poll_interval = poll_interval
        self.debounce_delay = debounce_delay

        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._debounce_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

        # file_path -> mtime_ns
        self._file_mtimes: Dict[Path, int] = {}
        # pending modified paths
        self._pending_events: Dict[Path, str] = {}
        self._last_change_time: float = 0.0

        self.callbacks: List[Callable[[List[Dict[str, Any]]], None]] = []

    def register_callback(self, cb: Callable[[List[Dict[str, Any]]], None]) -> None:
        with self._lock:
            self.callbacks.append(cb)

    def _should_watch(self, path: Path) -> bool:
        if path.name.startswith((".", "#", "~")):
            return False
        if any(part in {"__pycache__", ".git", ".pytest_cache", "node_modules"} for part in path.parts):
            return False
        return path.suffix.lower() in self.extensions

    def scan_snapshot(self) -> Dict[Path, int]:
        """Perform a single scan of watched directories, returning {Path: mtime_ns}."""
        snapshot: Dict[Path, int] = {}
        for root_dir in self.watch_dirs:
            if not root_dir.exists():
                continue
            for root, dirs, files in os.walk(root_dir, followlinks=True):
                # Avoid recursing into unwanted hidden or build directories
                dirs[:] = [d for d in dirs if not d.startswith((".", "__")) and d not in {"node_modules", "obj", "bin"}]
                for file_name in files:
                    p = Path(root) / file_name
                    if self._should_watch(p):
                        try:
                            # follow_symlinks=True to track underlying target edits
                            st = p.stat()
                            snapshot[p.resolve()] = st.st_mtime_ns
                        except OSError:
                            continue
        return snapshot

    def check_changes(self) -> Dict[Path, str]:
        """Compare current files against known snapshot and return changes {path: event_type}."""
        current = self.scan_snapshot()
        changes: Dict[Path, str] = {}

        with self._lock:
            # Check for modified or new files
            for p, mtime in current.items():
                if p not in self._file_mtimes:
                    if self._file_mtimes:  # Don't fire "created" on initial scan
                        changes[p] = "created"
                    self._file_mtimes[p] = mtime
                elif self._file_mtimes[p] != mtime:
                    changes[p] = "modified"
                    self._file_mtimes[p] = mtime

            # Check for deleted files
            deleted = set(self._file_mtimes.keys()) - set(current.keys())
            for p in deleted:
                changes[p] = "deleted"
                del self._file_mtimes[p]

        return changes

    def start(self) -> "FileWatcher":
        if self._running:
            return self

        # Initial baseline scan without firing events
        with self._lock:
            self._file_mtimes = self.scan_snapshot()

        self._running = True
        self._thread = threading.Thread(target=self._watch_loop, name="NaviFileWatcher", daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=1.0)
            self._thread = None

    def is_alive(self) -> bool:
        return self._running and self._thread is not None and self._thread.is_alive()

    def _watch_loop(self) -> None:
        while self._running:
            time.sleep(self.poll_interval)
            try:
                changes = self.check_changes()
                if changes:
                    now = time.time()
                    with self._lock:
                        self._pending_events.update(changes)
                        self._last_change_time = now

                    # Start or ensure debounce timer is running
                    if not self._debounce_thread or not self._debounce_thread.is_alive():
                        self._debounce_thread = threading.Thread(
                            target=self._debounce_drain_loop,
                            name="NaviFileWatcherDebounce",
                            daemon=True,
                        )
                        self._debounce_thread.start()
            except Exception:
                pass

    def _debounce_drain_loop(self) -> None:
        while self._running:
            time.sleep(0.05)
            with self._lock:
                elapsed = time.time() - self._last_change_time
                if elapsed >= self.debounce_delay and self._pending_events:
                    events_to_emit = dict(self._pending_events)
                    self._pending_events.clear()
                    break
                elif not self._pending_events:
                    return

        # Format events
        formatted: List[Dict[str, Any]] = []
        for path, ev_type in events_to_emit.items():
            formatted.append({
                "path": str(path),
                "rel_path": str(path.relative_to(Path.cwd())) if path.is_relative_to(Path.cwd()) else str(path),
                "name": path.name,
                "event": ev_type,
                "timestamp": time.time(),
            })

        # Dispatch callbacks
        with self._lock:
            cbs = list(self.callbacks)

        for cb in cbs:
            try:
                cb(formatted)
            except Exception:
                pass

