"""
Data models for project code map generation.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Dict, List, Optional


class Visibility(str, Enum):
    PUBLIC = "public"
    PRIVATE = "private"
    PROTECTED = "protected"
    INTERNAL = "internal"
    EXPORTED = "exported"


class SymbolKind(str, Enum):
    MODULE = "module"
    CLASS = "class"
    STRUCT = "struct"
    INTERFACE = "interface"
    TYPE = "type"
    FUNCTION = "function"
    METHOD = "method"
    NESTED_FUNCTION = "nested_function"
    ROUTE = "route"
    ENUM = "enum"


@dataclass
class ParameterInfo:
    name: str
    type_annotation: Optional[str] = None
    default_value: Optional[str] = None
    is_required: bool = True
    kind: str = "positional_or_keyword"

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "type": self.type_annotation,
            "default": self.default_value,
            "required": self.is_required,
            "kind": self.kind,
        }


@dataclass
class SymbolInfo:
    id: str
    name: str
    qualified_name: str
    kind: SymbolKind
    file_path: str
    start_line: int
    end_line: int
    signature: str
    parameters: List[ParameterInfo] = field(default_factory=list)
    return_type: Optional[str] = None
    visibility: Visibility = Visibility.PUBLIC
    modifiers: List[str] = field(default_factory=list)
    docstring: Optional[str] = None
    parent_id: Optional[str] = None
    children_ids: List[str] = field(default_factory=list)
    inherits: List[str] = field(default_factory=list)
    callees_raw: List[str] = field(default_factory=list)
    type_refs_raw: List[str] = field(default_factory=list)
    resolved_callees: List[str] = field(default_factory=list)
    resolved_callers: List[str] = field(default_factory=list)
    uncertain_callees: List[str] = field(default_factory=list)
    body_preview: Optional[str] = None
    is_nested: bool = False

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "name": self.name,
            "qualified_name": self.qualified_name,
            "kind": self.kind.value if isinstance(self.kind, SymbolKind) else str(self.kind),
            "file_path": self.file_path,
            "start_line": self.start_line,
            "end_line": self.end_line,
            "signature": self.signature,
            "parameters": [p.to_dict() for p in self.parameters],
            "return_type": self.return_type,
            "visibility": self.visibility.value if isinstance(self.visibility, Visibility) else str(self.visibility),
            "modifiers": self.modifiers,
            "docstring": self.docstring,
            "parent_id": self.parent_id,
            "children_ids": self.children_ids,
            "inherits": self.inherits,
            "callees": self.resolved_callees,
            "callers": self.resolved_callers,
            "uncertain_callees": self.uncertain_callees,
            "type_references": self.type_refs_raw,
            "is_nested": self.is_nested,
            "body_preview": self.body_preview,
        }


@dataclass
class ImportInfo:
    module: str
    imported_symbols: List[str] = field(default_factory=list)
    alias_map: Dict[str, str] = field(default_factory=dict)
    is_internal: bool = False
    line_number: int = 1

    def to_dict(self) -> Dict[str, Any]:
        return {
            "module": self.module,
            "imported_symbols": self.imported_symbols,
            "alias_map": self.alias_map,
            "is_internal": self.is_internal,
            "line_number": self.line_number,
        }


@dataclass
class FileInfo:
    path: str
    module_name: str
    language: str
    line_count: int
    size_bytes: int
    docstring: Optional[str] = None
    imports: List[ImportInfo] = field(default_factory=list)
    exports: List[str] = field(default_factory=list)
    symbol_ids: List[str] = field(default_factory=list)
    structural_summary: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "path": self.path,
            "module_name": self.module_name,
            "language": self.language,
            "line_count": self.line_count,
            "size_bytes": self.size_bytes,
            "docstring": self.docstring,
            "imports": [i.to_dict() for i in self.imports],
            "exports": self.exports,
            "symbol_ids": self.symbol_ids,
            "structural_summary": self.structural_summary,
        }


@dataclass
class ParseConfig:
    root_path: Path
    output_path: Path
    include_patterns: List[str] = field(default_factory=list)
    exclude_patterns: List[str] = field(default_factory=list)
    language: Optional[str] = None
    max_depth: Optional[int] = None
    include_private: bool = False
    include_bodies: bool = False
    output_format: str = "markdown"
    watch: bool = False
    include_timestamps: bool = False
    generation_command: str = "scripts/project_map.py generate"


@dataclass
class ProjectMap:
    project_name: str
    root_path: str
    generation_command: str
    config: ParseConfig
    languages: List[str] = field(default_factory=list)
    files: Dict[str, FileInfo] = field(default_factory=dict)
    symbols: Dict[str, SymbolInfo] = field(default_factory=dict)
    tree_text: str = ""
    call_graph: Dict[str, List[str]] = field(default_factory=dict)
    reverse_call_graph: Dict[str, List[str]] = field(default_factory=dict)
    external_dependencies: List[str] = field(default_factory=list)
    unresolved_references: List[Dict[str, Any]] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    stats: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "project_name": self.project_name,
            "root_path": self.root_path,
            "generation_command": self.generation_command,
            "languages": self.languages,
            "stats": self.stats,
            "tree": self.tree_text,
            "external_dependencies": self.external_dependencies,
            "files": {path: file_info.to_dict() for path, file_info in sorted(self.files.items())},
            "symbols": {sym_id: sym_info.to_dict() for sym_id, sym_info in sorted(self.symbols.items())},
            "call_graph": {k: sorted(v) for k, v in sorted(self.call_graph.items())},
            "unresolved_references": self.unresolved_references,
            "warnings": self.warnings,
        }
