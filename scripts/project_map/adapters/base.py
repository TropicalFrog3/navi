"""
Base class interface for language-specific AST and code structure adapters.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import List, Tuple

from ..models import FileInfo, ParseConfig, SymbolInfo


class BaseLanguageAdapter(ABC):
    """
    Abstract base class for all language-aware source code parsers.
    """

    language_id: str = "generic"
    file_extensions: List[str] = []

    def can_handle(self, file_path: Path) -> bool:
        """Return True if this adapter can process the given file."""
        ext = file_path.suffix.lower()
        return ext in self.file_extensions

    @abstractmethod
    def parse(
        self,
        file_path: Path,
        rel_path: str,
        content: str,
        config: ParseConfig,
    ) -> Tuple[FileInfo, List[SymbolInfo], List[str]]:
        """
        Parses source code into structured FileInfo and SymbolInfo instances.
        Returns:
            Tuple of (file_info, symbols_list, warnings_list)
        """
        pass
