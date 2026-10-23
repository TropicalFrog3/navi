"""
JavaScript and TypeScript language adapter parsing classes, interfaces, types, functions, methods, imports, and exports.
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

# Regex patterns for JS/TS constructs
RE_IMPORT_ESM = re.compile(r'^\s*import\s+(?:(\*\s+as\s+[a-zA-Z0-9_$]+)|(\{?[^{};]*\}?))\s+from\s+[\'"]([^\'"]+)[\'"]')
RE_EXPORT_DEFAULT = re.compile(r'^\s*export\s+default\s+(?:(class|function)\s+([a-zA-Z0-9_$]+)|([a-zA-Z0-9_$]+))')
RE_CLASS = re.compile(
    r'^\s*(export\s+)?(?:(abstract)\s+)?(class|interface)\s+([a-zA-Z0-9_$]+)'
    r'(?:\s+extends\s+([^{]+))?(?:\s+implements\s+([^{]+))?\s*\{'
)
RE_TYPE_ALIAS = re.compile(r'^\s*(export\s+)?type\s+([a-zA-Z0-9_$]+)(?:<[^>]+>)?\s*=\s*([^;]+);')
RE_ENUM = re.compile(r'^\s*(export\s+)?enum\s+([a-zA-Z0-9_$]+)\s*\{')
RE_FUNCTION = re.compile(
    r'^\s*(export\s+)?(?:(async)\s+)?function\s*(\*)?\s*([a-zA-Z0-9_$]+)\s*\(([^)]*)\)(?:\s*:\s*([^{]+))?\s*\{'
)
RE_CONST_ARROW = re.compile(
    r'^\s*(export\s+)?(?:const|let|var)\s+([a-zA-Z0-9_$]+)\s*=\s*(?:(async)\s+)?\(([^)]*)\)(?:\s*:\s*(.+?))?\s*=>'
)
RE_METHOD = re.compile(
    r'^\s*(public|private|protected)?\s*(static\s+)?(async\s+)?([a-zA-Z0-9_$]+)\s*\(([^)]*)\)(?:\s*:\s*([^{;]+))?\s*\{'
)


class JsTsAdapter(BaseLanguageAdapter):
    """
    Language adapter for JavaScript and TypeScript files (.js, .jsx, .ts, .tsx, .mjs, .cjs).
    """

    language_id = "javascript"
    file_extensions = [".js", ".jsx", ".ts", ".tsx", ".mjs", ".cjs"]

    def parse(
        self,
        file_path: Path,
        rel_path: str,
        content: str,
        config: ParseConfig,
    ) -> Tuple[FileInfo, List[SymbolInfo], List[str]]:
        warnings: List[str] = []
        lines = content.splitlines()

        is_ts = file_path.suffix.lower() in (".ts", ".tsx")
        lang_id = "typescript" if is_ts else "javascript"

        imports = self._extract_imports(lines)
        exports: List[str] = []
        symbols = self._extract_symbols(lines, rel_path, exports, config)

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

        class_count = sum(1 for s in symbols if s.kind in (SymbolKind.CLASS, SymbolKind.INTERFACE))
        func_count = sum(1 for s in symbols if s.kind in (SymbolKind.FUNCTION, SymbolKind.METHOD))
        summary = f"{len(lines)} lines, {class_count} classes/interfaces, {func_count} functions/methods"

        file_doc = self._extract_file_docstring(lines)

        file_info = FileInfo(
            path=rel_path,
            module_name=Path(rel_path).stem,
            language=lang_id,
            line_count=len(lines),
            size_bytes=len(content.encode("utf-8")),
            docstring=file_doc,
            imports=imports,
            exports=sorted(list(set(exports))),
            symbol_ids=[s.id for s in symbols],
            structural_summary=summary,
        )

        return file_info, symbols, warnings

    def _extract_imports(self, lines: List[str]) -> List[ImportInfo]:
        imports: List[ImportInfo] = []
        for line_no, line in enumerate(lines, start=1):
            match = RE_IMPORT_ESM.match(line)
            if match:
                symbols_part = match.group(2) or match.group(1) or ""
                mod_path = match.group(3)
                is_internal = mod_path.startswith(".") or mod_path.startswith("/")

                symbols = []
                alias_map = {}
                clean_syms = symbols_part.strip("{} * \t")
                if clean_syms:
                    for s in clean_syms.split(","):
                        s = s.strip()
                        if not s:
                            continue
                        if " as " in s:
                            orig, alias = s.split(" as ")
                            symbols.append(orig.strip())
                            alias_map[orig.strip()] = alias.strip()
                        else:
                            symbols.append(s)

                imports.append(
                    ImportInfo(
                        module=mod_path,
                        imported_symbols=symbols,
                        alias_map=alias_map,
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
            if trimmed.startswith("/**") or trimmed.startswith("*") or trimmed.startswith("//"):
                clean = trimmed.lstrip("/* \t").rstrip("*/ \t")
                if clean and not clean.startswith("@"):
                    doc_lines.append(clean)
            elif not trimmed:
                if doc_lines:
                    break
            else:
                break
        return " ".join(doc_lines[:2]) if doc_lines else None

    def _extract_symbols(
        self,
        lines: List[str],
        rel_path: str,
        exports_out: List[str],
        config: ParseConfig,
    ) -> List[SymbolInfo]:
        symbols: List[SymbolInfo] = []
        scope_stack: List[Tuple[str, str, SymbolKind]] = []
        # (scope_id, qualified_prefix, kind)

        brace_depth = 0
        scope_depth_map: List[int] = []

        for line_idx, line in enumerate(lines):
            line_no = line_idx + 1
            trimmed = line.strip()

            if trimmed.startswith("//") or trimmed.startswith("/*") or trimmed.startswith("*"):
                continue

            # Check Type Alias
            type_match = RE_TYPE_ALIAS.match(line)
            if type_match:
                is_exp = bool(type_match.group(1))
                type_name = type_match.group(2)
                type_def = type_match.group(3).strip()
                if is_exp:
                    exports_out.append(type_name)
                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                parent_id = scope_stack[-1][0] if scope_stack else None
                qname = f"{parent_prefix}.{type_name}" if parent_prefix else type_name
                sym_id = f"{rel_path}::{qname}"

                symbols.append(
                    SymbolInfo(
                        id=sym_id,
                        name=type_name,
                        qualified_name=qname,
                        kind=SymbolKind.TYPE,
                        file_path=rel_path,
                        start_line=line_no,
                        end_line=line_no,
                        signature=f"type {type_name} = {type_def}",
                        parameters=[],
                        return_type=None,
                        visibility=Visibility.EXPORTED if is_exp else Visibility.PUBLIC,
                        modifiers=["export"] if is_exp else [],
                        docstring=self._find_jsdoc_before(lines, line_idx),
                        parent_id=parent_id,
                        children_ids=[],
                        inherits=[],
                        callees_raw=[],
                        type_refs_raw=[type_def],
                        body_preview=None,
                        is_nested=bool(parent_id),
                    )
                )
                continue

            # Check Enum
            enum_match = RE_ENUM.match(line)
            if enum_match:
                is_exp = bool(enum_match.group(1))
                enum_name = enum_match.group(2)
                if is_exp:
                    exports_out.append(enum_name)
                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                parent_id = scope_stack[-1][0] if scope_stack else None
                qname = f"{parent_prefix}.{enum_name}" if parent_prefix else enum_name
                sym_id = f"{rel_path}::{qname}"

                symbols.append(
                    SymbolInfo(
                        id=sym_id,
                        name=enum_name,
                        qualified_name=qname,
                        kind=SymbolKind.ENUM,
                        file_path=rel_path,
                        start_line=line_no,
                        end_line=line_no,
                        signature=f"enum {enum_name}",
                        parameters=[],
                        return_type=None,
                        visibility=Visibility.EXPORTED if is_exp else Visibility.PUBLIC,
                        modifiers=["export"] if is_exp else [],
                        docstring=self._find_jsdoc_before(lines, line_idx),
                        parent_id=parent_id,
                        children_ids=[],
                        inherits=[],
                        callees_raw=[],
                        type_refs_raw=[],
                        body_preview=None,
                        is_nested=bool(parent_id),
                    )
                )
                scope_stack.append((sym_id, qname, SymbolKind.ENUM))
                scope_depth_map.append(brace_depth)
                brace_depth += line.count("{") - line.count("}")
                continue

            # Check Class / Interface
            class_match = RE_CLASS.match(line)
            if class_match:
                is_exp = bool(class_match.group(1))
                is_abstract = bool(class_match.group(2))
                kind_str = class_match.group(3)
                class_name = class_match.group(4)
                extends_str = class_match.group(5)
                implements_str = class_match.group(6)

                if is_exp:
                    exports_out.append(class_name)

                kind = SymbolKind.INTERFACE if kind_str == "interface" else SymbolKind.CLASS
                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                parent_id = scope_stack[-1][0] if scope_stack else None
                qname = f"{parent_prefix}.{class_name}" if parent_prefix else class_name
                sym_id = f"{rel_path}::{qname}"

                bases = []
                if extends_str:
                    bases.extend([b.strip() for b in extends_str.split(",")])
                if implements_str:
                    bases.extend([b.strip() for b in implements_str.split(",")])

                modifiers = []
                if is_exp:
                    modifiers.append("export")
                if is_abstract:
                    modifiers.append("abstract")

                sig = f"{' '.join(modifiers)} {kind_str} {class_name}".strip()
                if extends_str:
                    sig += f" extends {extends_str.strip()}"
                if implements_str:
                    sig += f" implements {implements_str.strip()}"

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
                    visibility=Visibility.EXPORTED if is_exp else Visibility.PUBLIC,
                    modifiers=modifiers,
                    docstring=self._find_jsdoc_before(lines, line_idx),
                    parent_id=parent_id,
                    children_ids=[],
                    inherits=bases,
                    callees_raw=[],
                    type_refs_raw=bases,
                    body_preview=None,
                    is_nested=bool(parent_id),
                )
                symbols.append(sym)
                scope_stack.append((sym_id, qname, kind))
                scope_depth_map.append(brace_depth)
                brace_depth += line.count("{") - line.count("}")
                continue

            # Check Function declaration
            func_match = RE_FUNCTION.match(line)
            if func_match:
                is_exp = bool(func_match.group(1))
                is_async = bool(func_match.group(2))
                is_gen = bool(func_match.group(3))
                func_name = func_match.group(4)
                params_str = func_match.group(5)
                ret_type = func_match.group(6).strip() if func_match.group(6) else None

                if is_exp:
                    exports_out.append(func_name)

                parent_id = scope_stack[-1][0] if scope_stack else None
                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                parent_kind = scope_stack[-1][2] if scope_stack else None

                kind = SymbolKind.NESTED_FUNCTION if parent_kind in (SymbolKind.FUNCTION, SymbolKind.METHOD) else SymbolKind.FUNCTION
                qname = f"{parent_prefix}.{func_name}" if parent_prefix else func_name
                sym_id = f"{rel_path}::{qname}"

                modifiers = []
                if is_exp:
                    modifiers.append("export")
                if is_async:
                    modifiers.append("async")
                if is_gen:
                    modifiers.append("generator")

                params, p_types = self._parse_js_params(params_str)
                type_refs = list(p_types)
                if ret_type:
                    type_refs.append(ret_type)

                sig = f"{' '.join(modifiers)} function {func_name}({params_str})".strip()
                if ret_type:
                    sig += f": {ret_type}"

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
                    return_type=ret_type,
                    visibility=Visibility.EXPORTED if is_exp else Visibility.PUBLIC,
                    modifiers=modifiers,
                    docstring=self._find_jsdoc_before(lines, line_idx),
                    parent_id=parent_id,
                    children_ids=[],
                    inherits=[],
                    callees_raw=callees,
                    type_refs_raw=sorted(list(set(type_refs))),
                    body_preview=None,
                    is_nested=bool(parent_id),
                )
                symbols.append(sym)
                scope_stack.append((sym_id, qname, kind))
                scope_depth_map.append(brace_depth)
                brace_depth += line.count("{") - line.count("}")
                continue

            # Check Const Arrow Function
            arrow_match = RE_CONST_ARROW.match(line)
            if arrow_match:
                is_exp = bool(arrow_match.group(1))
                func_name = arrow_match.group(2)
                is_async = bool(arrow_match.group(3))
                params_str = arrow_match.group(4)
                ret_type = arrow_match.group(5).strip() if arrow_match.group(5) else None

                if is_exp:
                    exports_out.append(func_name)

                parent_id = scope_stack[-1][0] if scope_stack else None
                parent_prefix = scope_stack[-1][1] if scope_stack else ""
                parent_kind = scope_stack[-1][2] if scope_stack else None

                kind = SymbolKind.NESTED_FUNCTION if parent_kind in (SymbolKind.FUNCTION, SymbolKind.METHOD) else SymbolKind.FUNCTION
                qname = f"{parent_prefix}.{func_name}" if parent_prefix else func_name
                sym_id = f"{rel_path}::{qname}"

                modifiers = ["const"]
                if is_exp:
                    modifiers.insert(0, "export")
                if is_async:
                    modifiers.append("async")

                params, p_types = self._parse_js_params(params_str)
                type_refs = list(p_types)
                if ret_type:
                    type_refs.append(ret_type)

                sig = f"{' '.join(modifiers)} {func_name} = ({params_str})".strip()
                if ret_type:
                    sig += f": {ret_type}"
                sig += " =>"

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
                    return_type=ret_type,
                    visibility=Visibility.EXPORTED if is_exp else Visibility.PUBLIC,
                    modifiers=modifiers,
                    docstring=self._find_jsdoc_before(lines, line_idx),
                    parent_id=parent_id,
                    children_ids=[],
                    inherits=[],
                    callees_raw=callees,
                    type_refs_raw=sorted(list(set(type_refs))),
                    body_preview=None,
                    is_nested=bool(parent_id),
                )
                symbols.append(sym)
                if "{" in line:
                    scope_stack.append((sym_id, qname, kind))
                    scope_depth_map.append(brace_depth)
                    brace_depth += line.count("{") - line.count("}")
                continue

            # Check Method (inside class/interface)
            if scope_stack and scope_stack[-1][2] in (SymbolKind.CLASS, SymbolKind.INTERFACE):
                method_match = RE_METHOD.match(line)
                if method_match and not trimmed.startswith("if") and not trimmed.startswith("for") and not trimmed.startswith("switch"):
                    vis_str = method_match.group(1) or "public"
                    is_static = bool(method_match.group(2))
                    is_async = bool(method_match.group(3))
                    method_name = method_match.group(4)
                    params_str = method_match.group(5)
                    ret_type = method_match.group(6).strip() if method_match.group(6) else None

                    parent_id = scope_stack[-1][0]
                    parent_prefix = scope_stack[-1][1]
                    qname = f"{parent_prefix}.{method_name}"
                    sym_id = f"{rel_path}::{qname}"

                    vis = Visibility.PRIVATE if vis_str == "private" else (Visibility.PROTECTED if vis_str == "protected" else Visibility.PUBLIC)
                    modifiers = []
                    if is_static:
                        modifiers.append("static")
                    if is_async:
                        modifiers.append("async")

                    params, p_types = self._parse_js_params(params_str)
                    type_refs = list(p_types)
                    if ret_type:
                        type_refs.append(ret_type)

                    sig = f"{vis_str} {' '.join(modifiers)} {method_name}({params_str})".strip()
                    if ret_type:
                        sig += f": {ret_type}"

                    callees = self._extract_calls(line)

                    sym = SymbolInfo(
                        id=sym_id,
                        name=method_name,
                        qualified_name=qname,
                        kind=SymbolKind.METHOD,
                        file_path=rel_path,
                        start_line=line_no,
                        end_line=line_no,
                        signature=sig,
                        parameters=params,
                        return_type=ret_type,
                        visibility=vis,
                        modifiers=modifiers,
                        docstring=self._find_jsdoc_before(lines, line_idx),
                        parent_id=parent_id,
                        children_ids=[],
                        inherits=[],
                        callees_raw=callees,
                        type_refs_raw=sorted(list(set(type_refs))),
                        body_preview=None,
                        is_nested=False,
                    )
                    symbols.append(sym)
                    scope_stack.append((sym_id, qname, SymbolKind.METHOD))
                    scope_depth_map.append(brace_depth)
                    brace_depth += line.count("{") - line.count("}")
                    continue

            # Update brace depth
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

    def _parse_js_params(self, params_str: str) -> Tuple[List[ParameterInfo], List[str]]:
        params: List[ParameterInfo] = []
        type_refs: List[str] = []
        if not params_str or not params_str.strip():
            return params, type_refs

        parts = [p.strip() for p in params_str.split(",") if p.strip()]
        for p in parts:
            default_val = None
            if "=" in p:
                p, default_val = [x.strip() for x in p.split("=", 1)]

            p_type = None
            if ":" in p:
                p_name, p_type = [x.strip() for x in p.split(":", 1)]
                type_refs.append(p_type)
            else:
                p_name = p.strip()

            is_var = p_name.startswith("...")
            if is_var:
                p_name = p_name[3:]

            params.append(
                ParameterInfo(
                    name=p_name,
                    type_annotation=p_type,
                    default_value=default_val,
                    is_required=default_val is None and not is_var,
                    kind="var_positional" if is_var else "positional_or_keyword",
                )
            )
        return params, type_refs

    def _find_jsdoc_before(self, lines: List[str], line_idx: int) -> Optional[str]:
        comments = []
        curr = line_idx - 1
        while curr >= 0:
            trimmed = lines[curr].strip()
            if trimmed.startswith("/**") or trimmed.startswith("*") or trimmed.startswith("//"):
                clean = trimmed.lstrip("/* \t").rstrip("*/ \t")
                if clean and not clean.startswith("@"):
                    comments.insert(0, clean)
                curr -= 1
            else:
                break
        return " ".join(comments[:2]) if comments else None

    def _extract_calls(self, line: str) -> List[str]:
        matches = re.findall(r'([a-zA-Z0-9_$]+)\s*\(', line)
        keywords = {"if", "for", "while", "switch", "catch", "function", "import", "require"}
        return [m for m in matches if m not in keywords]
