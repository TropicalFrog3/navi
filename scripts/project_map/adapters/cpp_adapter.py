"""
C/C++ language adapter parsing namespaces, classes, structs, methods, functions, and #includes.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import List, Optional, Set, Tuple

from ..models import (
    FileInfo,
    ImportInfo,
    ParameterInfo,
    ParseConfig,
    SymbolInfo,
    SymbolKind,
    Visibility,
)
from .base import BaseLanguageAdapter

# Regex patterns for C/C++ constructs
RE_INCLUDE = re.compile(r'^\s*#include\s+(["<])([^">]+)([">])')
RE_NAMESPACE = re.compile(r'^\s*namespace\s+([a-zA-Z0-9_:]+)\s*\{')
RE_CLASS = re.compile(
    r'^\s*(template\s*<[^>]+>\s*)?(class|struct)\s+([a-zA-Z0-9_]+)'
    r'(?:\s*:\s*([^{;]+))?\s*\{'
)
RE_FUNC = re.compile(
    r'^\s*(?:(virtual|static|inline|explicit|constexpr)\s+)*'
    r'([a-zA-Z0-9_:<>&*~]+(?:\s*<[^>]+>)?(?:\s*[*&]+)?)\s+'
    r'([a-zA-Z0-9_:]+)\s*\(([^)]*)\)\s*(const)?\s*(override|final)?\s*(noexcept)?\s*(\{|;)'
)
RE_LAMBDA = re.compile(r'\[(.*?)\]\s*\((.*?)\)\s*(?:->\s*([^{]+))?\s*\{')


class CppAdapter(BaseLanguageAdapter):
    """
    Language adapter for C and C++ files (.cpp, .hpp, .cc, .h, .cxx, .c).
    """

    language_id = "cpp"
    file_extensions = [".cpp", ".hpp", ".cc", ".h", ".cxx", ".c"]

    def parse(
        self,
        file_path: Path,
        rel_path: str,
        content: str,
        config: ParseConfig,
    ) -> Tuple[FileInfo, List[SymbolInfo], List[str]]:
        warnings: List[str] = []
        lines = content.splitlines()

        imports = self._extract_includes(lines, config)
        symbols = self._extract_symbols(lines, rel_path, config)

        if not config.include_private:
            filtered_symbols = []
            allowed_ids: Set[str] = set()
            for s in symbols:
                if s.visibility in (Visibility.PUBLIC, Visibility.EXPORTED):
                    filtered_symbols.append(s)
                    allowed_ids.add(s.id)
            for s in filtered_symbols:
                s.children_ids = [cid for cid in s.children_ids if cid in allowed_ids]
            symbols = filtered_symbols

        class_count = sum(1 for s in symbols if s.kind in (SymbolKind.CLASS, SymbolKind.STRUCT))
        func_count = sum(1 for s in symbols if s.kind in (SymbolKind.FUNCTION, SymbolKind.METHOD))
        summary = f"{len(lines)} lines, {class_count} classes/structs, {func_count} functions/methods"

        file_doc = self._extract_file_docstring(lines)

        file_info = FileInfo(
            path=rel_path,
            module_name=Path(rel_path).stem,
            language=self.language_id,
            line_count=len(lines),
            size_bytes=len(content.encode("utf-8")),
            docstring=file_doc,
            imports=imports,
            exports=[],
            symbol_ids=[s.id for s in symbols],
            structural_summary=summary,
        )

        return file_info, symbols, warnings

    def _extract_includes(self, lines: List[str], config: ParseConfig) -> List[ImportInfo]:
        imports: List[ImportInfo] = []
        for line_no, line in enumerate(lines, start=1):
            match = RE_INCLUDE.match(line)
            if match:
                delimiter_open = match.group(1)
                header_path = match.group(2)
                # Delimiter " means local include, < means system/external include
                is_internal = delimiter_open == '"'
                if not is_internal:
                    # check if header exists in project include dirs
                    if (config.root_path / header_path).exists() or (config.root_path / "include" / header_path).exists():
                        is_internal = True

                imports.append(
                    ImportInfo(
                        module=header_path,
                        imported_symbols=[Path(header_path).name],
                        alias_map={},
                        is_internal=is_internal,
                        line_number=line_no,
                    )
                )
        imports.sort(key=lambda i: (i.line_number, i.module))
        return imports

    def _extract_file_docstring(self, lines: List[str]) -> Optional[str]:
        doc_lines = []
        for line in lines[:25]:
            trimmed = line.strip()
            if trimmed.startswith("//") or trimmed.startswith("/*") or trimmed.startswith("*"):
                clean = trimmed.lstrip("/* \t").rstrip("*/ \t")
                if clean:
                    doc_lines.append(clean)
            elif not trimmed:
                if doc_lines:
                    break
            else:
                break
        return " ".join(doc_lines[:2]) if doc_lines else None

    def _extract_symbols(
        self, lines: List[str], rel_path: str, config: ParseConfig
    ) -> List[SymbolInfo]:
        symbols: List[SymbolInfo] = []
        scope_stack: List[Tuple[str, str, SymbolKind, Visibility]] = []
        # (scope_id, qualified_prefix, kind, current_visibility)

        # Track brace depth for scope popping
        brace_depth = 0
        scope_depth_map: List[int] = []

        current_class_visibility = Visibility.PUBLIC

        for line_idx, line in enumerate(lines):
            line_no = line_idx + 1
            trimmed = line.strip()

            # Ignore preprocessor directives except namespaces/classes
            if trimmed.startswith("#"):
                continue

            # Check visibility labels in classes
            if trimmed in ("public:", "private:", "protected:"):
                if trimmed == "public:":
                    current_class_visibility = Visibility.PUBLIC
                elif trimmed == "protected:":
                    current_class_visibility = Visibility.PROTECTED
                else:
                    current_class_visibility = Visibility.PRIVATE
                continue

            # Check namespace
            ns_match = RE_NAMESPACE.match(line)
            if ns_match:
                ns_name = ns_match.group(1)
                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                qname = f"{parent_prefix}::{ns_name}" if parent_prefix else ns_name
                scope_stack.append((f"{rel_path}::{qname}", qname, SymbolKind.MODULE, Visibility.PUBLIC))
                scope_depth_map.append(brace_depth)
                brace_depth += line.count("{") - line.count("}")
                continue

            # Check class or struct
            class_match = RE_CLASS.match(line)
            if class_match:
                template_prefix = class_match.group(1) or ""
                kind_str = class_match.group(2)
                class_name = class_match.group(3)
                bases_str = class_match.group(4)

                kind = SymbolKind.STRUCT if kind_str == "struct" else SymbolKind.CLASS
                # Default visibility: structs are public, classes are private
                current_class_visibility = Visibility.PUBLIC if kind == SymbolKind.STRUCT else Visibility.PRIVATE

                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                parent_id = scope_stack[-1][0] if scope_stack else None
                qname = f"{parent_prefix}::{class_name}" if parent_prefix else class_name
                sym_id = f"{rel_path}::{qname}"

                bases = [b.strip() for b in bases_str.split(",")] if bases_str else []

                doc = self._find_doc_comment_before(lines, line_idx)
                sig = f"{template_prefix}{kind_str} {class_name}"
                if bases:
                    sig += f" : {', '.join(bases)}"

                sym = SymbolInfo(
                    id=sym_id,
                    name=class_name,
                    qualified_name=qname,
                    kind=kind,
                    file_path=rel_path,
                    start_line=line_no,
                    end_line=line_no,
                    signature=sig,
                    parameters=[],
                    return_type=None,
                    visibility=Visibility.PUBLIC,
                    modifiers=[template_prefix.strip()] if template_prefix else [],
                    docstring=doc,
                    parent_id=parent_id,
                    children_ids=[],
                    inherits=bases,
                    callees_raw=[],
                    type_refs_raw=bases,
                    body_preview=None,
                    is_nested=bool(parent_id),
                )
                symbols.append(sym)
                scope_stack.append((sym_id, qname, kind, current_class_visibility))
                scope_depth_map.append(brace_depth)
                brace_depth += line.count("{") - line.count("}")
                continue

            # Check functions & methods
            func_match = RE_FUNC.match(line)
            if func_match and not trimmed.startswith("return") and not trimmed.startswith("if"):
                modifier = func_match.group(1) or ""
                return_type = func_match.group(2).strip()
                func_name = func_match.group(3).strip()
                params_str = func_match.group(4).strip()
                const_qualifier = func_match.group(5) or ""
                override_qualifier = func_match.group(6) or ""
                noexcept_qualifier = func_match.group(7) or ""

                # Distinguish method vs standalone function vs nested
                parent_id = None
                parent_prefix = ""
                parent_kind = None
                vis = Visibility.PUBLIC

                if scope_stack:
                    parent_id = scope_stack[-1][0]
                    parent_prefix = scope_stack[-1][1]
                    parent_kind = scope_stack[-1][2]
                    vis = current_class_visibility if parent_kind in (SymbolKind.CLASS, SymbolKind.STRUCT) else Visibility.PUBLIC

                if parent_kind in (SymbolKind.CLASS, SymbolKind.STRUCT):
                    kind = SymbolKind.METHOD
                    is_nested = False
                elif parent_kind in (SymbolKind.FUNCTION, SymbolKind.METHOD):
                    kind = SymbolKind.NESTED_FUNCTION
                    is_nested = True
                else:
                    kind = SymbolKind.FUNCTION
                    is_nested = False

                qname = f"{parent_prefix}::{func_name}" if parent_prefix else func_name
                sym_id = f"{rel_path}::{qname}"

                params, type_refs = self._parse_cpp_params(params_str)
                type_refs.append(return_type)

                modifiers = []
                if modifier:
                    modifiers.append(modifier)
                if const_qualifier:
                    modifiers.append("const")
                if override_qualifier:
                    modifiers.append(override_qualifier)
                if noexcept_qualifier:
                    modifiers.append(noexcept_qualifier)

                doc = self._find_doc_comment_before(lines, line_idx)
                sig = f"{return_type} {func_name}({params_str})"
                if const_qualifier:
                    sig += f" {const_qualifier}"
                if override_qualifier:
                    sig += f" {override_qualifier}"

                callees = self._extract_calls(line)

                sym = SymbolInfo(
                    id=sym_id,
                    name=func_name,
                    qualified_name=qname,
                    kind=kind,
                    file_path=rel_path,
                    start_line=line_no,
                    end_line=line_no,
                    signature=sig,
                    parameters=params,
                    return_type=return_type,
                    visibility=vis,
                    modifiers=modifiers,
                    docstring=doc,
                    parent_id=parent_id,
                    children_ids=[],
                    inherits=[],
                    callees_raw=callees,
                    type_refs_raw=sorted(list(set(type_refs))),
                    body_preview=None,
                    is_nested=is_nested,
                )
                symbols.append(sym)

            # Update brace count and pop scope stack if leaving a block
            open_braces = line.count("{")
            close_braces = line.count("}")
            brace_depth += open_braces - close_braces

            while scope_depth_map and brace_depth <= scope_depth_map[-1]:
                scope_stack.pop()
                scope_depth_map.pop()

        # Update children_ids
        sym_by_id = {s.id: s for s in symbols}
        for s in symbols:
            if s.parent_id and s.parent_id in sym_by_id:
                sym_by_id[s.parent_id].children_ids.append(s.id)

        return symbols

    def _parse_cpp_params(self, params_str: str) -> Tuple[List[ParameterInfo], List[str]]:
        params: List[ParameterInfo] = []
        type_refs: List[str] = []
        if not params_str or params_str.strip() == "void":
            return params, type_refs

        parts = [p.strip() for p in params_str.split(",") if p.strip()]
        for p in parts:
            default_val = None
            if "=" in p:
                p, default_val = [x.strip() for x in p.split("=", 1)]

            tokens = p.split()
            if len(tokens) >= 2:
                p_name = tokens[-1].lstrip("*&")
                p_type = " ".join(tokens[:-1])
                type_refs.append(p_type)
            else:
                p_name = tokens[0] if tokens else "param"
                p_type = None

            params.append(
                ParameterInfo(
                    name=p_name,
                    type_annotation=p_type,
                    default_value=default_val,
                    is_required=default_val is None,
                    kind="positional_or_keyword",
                )
            )
        return params, type_refs

    def _find_doc_comment_before(self, lines: List[str], line_idx: int) -> Optional[str]:
        comments = []
        curr = line_idx - 1
        while curr >= 0:
            trimmed = lines[curr].strip()
            if trimmed.startswith("///") or trimmed.startswith("//") or trimmed.startswith("*") or trimmed.startswith("/*"):
                clean = trimmed.lstrip("/* / \t").rstrip("*/ \t")
                if clean:
                    comments.insert(0, clean)
                curr -= 1
            else:
                break
        return " ".join(comments[:2]) if comments else None

    def _extract_calls(self, line: str) -> List[str]:
        # Simple call extraction: identifier followed by (
        matches = re.findall(r'([a-zA-Z0-9_:]+)\s*\(', line)
        keywords = {"if", "for", "while", "switch", "catch", "sizeof", "decltype"}
        return [m for m in matches if m not in keywords]
