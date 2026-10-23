"""
Language adapter implementations and registry.
"""

from .base import BaseLanguageAdapter
from .registry import get_adapter_for_file, register_adapter, get_all_adapters

__all__ = [
    "BaseLanguageAdapter",
    "get_adapter_for_file",
    "register_adapter",
    "get_all_adapters",
]
