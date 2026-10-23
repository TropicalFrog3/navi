"""
Language adapter registry and factory.
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional

from .base import BaseLanguageAdapter
from .python_adapter import PythonAdapter
from .cpp_adapter import CppAdapter
from .js_ts_adapter import JsTsAdapter

_ADAPTERS: List[BaseLanguageAdapter] = [
    PythonAdapter(),
    CppAdapter(),
    JsTsAdapter(),
]


def register_adapter(adapter: BaseLanguageAdapter) -> None:
    """Register a new language adapter."""
    _ADAPTERS.insert(0, adapter)


def get_all_adapters() -> List[BaseLanguageAdapter]:
    """Return all registered language adapters."""
    return list(_ADAPTERS)


def get_adapter_for_file(file_path: Path, forced_language: Optional[str] = None) -> Optional[BaseLanguageAdapter]:
    """Find the appropriate adapter for a file, considering optional language override."""
    if forced_language and forced_language != "all":
        target = forced_language.lower()
        for adapter in _ADAPTERS:
            if adapter.language_id.lower() == target:
                return adapter

    for adapter in _ADAPTERS:
        if adapter.can_handle(file_path):
            return adapter

    return None
