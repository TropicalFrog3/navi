"""
Python AST language adapter utilizing Python standard library `ast`.
"""

from __future__ import annotations

import ast
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

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


def _safe_unparse(node: Optional[ast.AST]) -> Optional[str]:
    """Safely convert AST node back to source representation."""
    if node is None:
        return None
    try:
        return ast.unparse(node)
    except Exception:
        return None


def _clean_docstring(doc: Optional[str]) -> Optional[str]:
    """Clean docstring to a concise single or multi-line summary."""
    if not doc:
        return None
    lines = [line.strip() for line in doc.strip().splitlines() if line.strip()]
    if not lines:
        return None
    # If single line or short, return first paragraph summary
    summary = lines[0]
    if len(lines) > 1 and not summary.endswith("."):
        summary += f" ({lines[1]})"
    return summary[:200]


def _determine_visibility(name: str, is_exported: bool = False) -> Visibility:
    """Determine visibility based on Python naming conventions."""
    if is_exported:
        return Visibility.EXPORTED
    if name.startswith("__") and not name.endswith("__"):
        return Visibility.PRIVATE
    if name.startswith("_") and not name.endswith("__"):
        return Visibility.PROTECTED
    return Visibility.PUBLIC


def _extract_type_names(node: Optional[ast.AST]) -> List[str]:
    """Extract named types referenced within a type annotation AST node."""
    if node is None:
        return []
    type_names: List[str] = []
    for sub in ast.walk(node):
        if isinstance(sub, ast.Name):
            type_names.append(sub.id)
        elif isinstance(sub, ast.Attribute) and isinstance(sub.value, ast.Name):
            type_names.append(f"{sub.value.id}.{sub.attr}")
    return sorted(list(set(type_names)))


class PythonAdapter(BaseLanguageAdapter):
    """
    Language adapter for Python source files using standard library `ast`.
    """

    language_id = "python"
    file_extensions = [".py", ".pyw", ".pyi"]

    def parse(
        self,
        file_path: Path,
        rel_path: str,
        content: str,
        config: ParseConfig,
    ) -> Tuple[FileInfo, List[SymbolInfo], List[str]]:
        warnings: List[str] = []
        try:
            tree = ast.parse(content, filename=str(file_path))
        except SyntaxError as e:
            warnings.append(f"SyntaxError in {rel_path}:{e.lineno}: {e.msg}")
            # Return basic file info with error
            lines = content.splitlines()
            file_info = FileInfo(
                path=rel_path,
                module_name=self._module_name_from_path(rel_path),
                language=self.language_id,
                line_count=len(lines),
                size_bytes=len(content.encode("utf-8")),
                docstring=None,
                imports=[],
                exports=[],
                symbol_ids=[],
                structural_summary="Failed to parse syntax",
            )
            return file_info, [], warnings

        source_lines = content.splitlines()
        file_doc = _clean_docstring(ast.get_docstring(tree))
        module_name = self._module_name_from_path(rel_path)

        # 1. Extract exports (__all__)
        exports = self._extract_exports(tree)

        # 2. Extract imports
        imports = self._extract_imports(tree, rel_path, config)

        # 3. Extract symbols hierarchically
        symbols, symbol_warnings = self._extract_symbols(
            tree=tree,
            rel_path=rel_path,
            module_name=module_name,
            exports=exports,
            source_lines=source_lines,
            config=config,
        )
        warnings.extend(symbol_warnings)

        # Filter private symbols if not config.include_private
        if not config.include_private:
            filtered_symbols: List[SymbolInfo] = []
            allowed_ids: Set[str] = set()
            for s in symbols:
                if s.visibility in (Visibility.PUBLIC, Visibility.EXPORTED):
                    filtered_symbols.append(s)
                    allowed_ids.add(s.id)

            # Clean children_ids for remaining symbols
            for s in filtered_symbols:
                s.children_ids = [cid for cid in s.children_ids if cid in allowed_ids]
            symbols = filtered_symbols

        class_count = sum(1 for s in symbols if s.kind == SymbolKind.CLASS)
        func_count = sum(1 for s in symbols if s.kind in (SymbolKind.FUNCTION, SymbolKind.METHOD))
        summary = f"{len(source_lines)} lines, {class_count} classes, {func_count} functions/methods"

        file_info = FileInfo(
            path=rel_path,
            module_name=module_name,
            language=self.language_id,
            line_count=len(source_lines),
            size_bytes=len(content.encode("utf-8")),
            docstring=file_doc,
            imports=imports,
            exports=exports,
            symbol_ids=[s.id for s in symbols],
            structural_summary=summary,
        )

        return file_info, symbols, warnings

    def _module_name_from_path(self, rel_path: str) -> str:
        """Derive Python module dot-path from file path."""
        p = Path(rel_path)
        parts = list(p.with_suffix("").parts)
        if parts and parts[-1] == "__init__":
            parts.pop()
        return ".".join(parts) if parts else p.stem

    def _extract_exports(self, tree: ast.Module) -> List[str]:
        """Extract __all__ exported symbols if defined."""
        for stmt in tree.body:
            if isinstance(stmt, ast.Assign):
                for target in stmt.targets:
                    if isinstance(target, ast.Name) and target.id == "__all__":
                        if isinstance(stmt.value, (ast.List, ast.Tuple, ast.Set)):
                            exports = []
                            for elt in stmt.value.elts:
                                if isinstance(elt, ast.Constant) and isinstance(elt.value, str):
                                    exports.append(elt.value)
                            return sorted(exports)
        return []

    def _extract_imports(
        self, tree: ast.Module, rel_path: str, config: ParseConfig
    ) -> List[ImportInfo]:
        """Extract imports with internal/external distinction."""
        imports: List[ImportInfo] = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    mod = alias.name
                    alias_map = {mod: alias.asname} if alias.asname else {}
                    is_internal = self._is_internal_module(mod, config)
                    imports.append(
                        ImportInfo(
                            module=mod,
                            imported_symbols=[alias.name],
                            alias_map=alias_map,
                            is_internal=is_internal,
                            line_number=node.lineno,
                        )
                    )
            elif isinstance(node, ast.ImportFrom):
                mod = node.module or ""
                # relative import: level > 0
                is_internal = node.level > 0 or self._is_internal_module(mod, config)
                syms = [a.name for a in node.names]
                alias_map = {a.name: a.asname for a in node.names if a.asname}
                full_mod = ("." * node.level) + mod if node.level else mod
                imports.append(
                    ImportInfo(
                        module=full_mod,
                        imported_symbols=syms,
                        alias_map=alias_map,
                        is_internal=is_internal,
                        line_number=node.lineno,
                    )
                )
        imports.sort(key=lambda i: (i.line_number, i.module))
        return imports

    def _is_internal_module(self, mod_name: str, config: ParseConfig) -> bool:
        """Check if module name corresponds to local project directory or file."""
        if not mod_name:
            return False
        top_name = mod_name.split(".")[0]
        # Check if directory or file exists in project root
        root = config.root_path
        if (root / top_name).exists() or (root / f"{top_name}.py").exists():
            return True
        return False

    def _extract_symbols(
        self,
        tree: ast.Module,
        rel_path: str,
        module_name: str,
        exports: List[str],
        source_lines: List[str],
        config: ParseConfig,
    ) -> Tuple[List[SymbolInfo], List[str]]:
        symbols: List[SymbolInfo] = []
        warnings: List[str] = []

        # Stack to track containment hierarchy:
        # Each item: (node, symbol_id, qualified_name, kind)
        def process_node(
            node: ast.AST,
            parent_id: Optional[str] = None,
            parent_qname: str = "",
            parent_kind: Optional[SymbolKind] = None,
        ):
            if isinstance(node, ast.ClassDef):
                sym = self._build_class_symbol(
                    node=node,
                    rel_path=rel_path,
                    module_name=module_name,
                    parent_id=parent_id,
                    parent_qname=parent_qname,
                    exports=exports,
                    source_lines=source_lines,
                    config=config,
                )
                symbols.append(sym)

                # Process class body items
                for child in node.body:
                    process_node(
                        node=child,
                        parent_id=sym.id,
                        parent_qname=sym.qualified_name,
                        parent_kind=SymbolKind.CLASS,
                    )

            elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                sym = self._build_function_symbol(
                    node=node,
                    rel_path=rel_path,
                    module_name=module_name,
                    parent_id=parent_id,
                    parent_qname=parent_qname,
                    parent_kind=parent_kind,
                    exports=exports,
                    source_lines=source_lines,
                    config=config,
                )
                symbols.append(sym)

                # Process nested functions / closures
                for child in node.body:
                    if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                        process_node(
                            node=child,
                            parent_id=sym.id,
                            parent_qname=sym.qualified_name,
                            parent_kind=sym.kind,
                        )

        for stmt in tree.body:
            process_node(stmt)

        # Update children_ids
        sym_by_id = {s.id: s for s in symbols}
        for s in symbols:
            if s.parent_id and s.parent_id in sym_by_id:
                sym_by_id[s.parent_id].children_ids.append(s.id)

        return symbols, warnings

    def _build_class_symbol(
        self,
        node: ast.ClassDef,
        rel_path: str,
        module_name: str,
        parent_id: Optional[str],
        parent_qname: str,
        exports: List[str],
        source_lines: List[str],
        config: ParseConfig,
    ) -> SymbolInfo:
        qname = f"{parent_qname}.{node.name}" if parent_qname else node.name
        sym_id = f"{rel_path}::{qname}"

        # Inheritance bases
        bases: List[str] = []
        type_refs: List[str] = []
        for b in node.bases:
            b_str = _safe_unparse(b)
            if b_str:
                bases.append(b_str)
            type_refs.extend(_extract_type_names(b))

        # Decorators / modifiers
        modifiers: List[str] = []
        for dec in node.decorator_list:
            dec_str = _safe_unparse(dec)
            if dec_str:
                modifiers.append(f"@{dec_str}")

        is_exported = node.name in exports
        visibility = _determine_visibility(node.name, is_exported)
        doc = _clean_docstring(ast.get_docstring(node))

        start_line = node.lineno
        end_line = getattr(node, "end_lineno", node.lineno)

        sig = f"class {node.name}"
        if bases:
            sig += f"({', '.join(bases)})"
        sig += ":"

        body_preview = None
        if config.include_bodies and start_line <= len(source_lines):
            # Take up to 5 lines of body
            preview_lines = source_lines[start_line - 1 : min(end_line, start_line + 4)]
            body_preview = "\n".join(preview_lines)

        return SymbolInfo(
            id=sym_id,
            name=node.name,
            qualified_name=qname,
            kind=SymbolKind.CLASS,
            file_path=rel_path,
            start_line=start_line,
            end_line=end_line,
            signature=sig,
            parameters=[],
            return_type=None,
            visibility=visibility,
            modifiers=modifiers,
            docstring=doc,
            parent_id=parent_id,
            children_ids=[],
            inherits=bases,
            callees_raw=[],
            type_refs_raw=sorted(list(set(type_refs))),
            body_preview=body_preview,
            is_nested=bool(parent_id),
        )

    def _build_function_symbol(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef,
        rel_path: str,
        module_name: str,
        parent_id: Optional[str],
        parent_qname: str,
        parent_kind: Optional[SymbolKind],
        exports: List[str],
        source_lines: List[str],
        config: ParseConfig,
    ) -> SymbolInfo:
        qname = f"{parent_qname}.{node.name}" if parent_qname else node.name
        sym_id = f"{rel_path}::{qname}"

        # Determine Kind
        if parent_kind == SymbolKind.CLASS:
            kind = SymbolKind.METHOD
            is_nested = False
        elif parent_kind in (SymbolKind.FUNCTION, SymbolKind.METHOD, SymbolKind.NESTED_FUNCTION):
            kind = SymbolKind.NESTED_FUNCTION
            is_nested = True
        else:
            kind = SymbolKind.FUNCTION
            is_nested = False

        # Modifiers & Decorators
        modifiers: List[str] = []
        is_route = False
        for dec in node.decorator_list:
            dec_str = _safe_unparse(dec)
            if dec_str:
                modifiers.append(f"@{dec_str}")
                if any(r in dec_str.lower() for r in ["route", "router.", "endpoint", "api_view", "get(", "post("]):
                    is_route = True

        if is_route and kind == SymbolKind.FUNCTION:
            kind = SymbolKind.ROUTE

        if isinstance(node, ast.AsyncFunctionDef):
            modifiers.insert(0, "async")

        # Parameters parsing
        parameters, param_type_refs = self._parse_parameters(node.args)

        # Return type
        return_type = _safe_unparse(node.returns)
        if node.returns:
            param_type_refs.extend(_extract_type_names(node.returns))

        # Build readable signature
        sig_prefix = "async def " if isinstance(node, ast.AsyncFunctionDef) else "def "
        param_strs = []
        for p in parameters:
            p_str = p.name
            if p.kind == "var_positional":
                p_str = f"*{p_str}"
            elif p.kind == "var_keyword":
                p_str = f"**{p_str}"
            if p.type_annotation:
                p_str += f": {p.type_annotation}"
            if p.default_value is not None:
                p_str += f" = {p.default_value}"
            param_strs.append(p_str)

        sig = f"{sig_prefix}{node.name}({', '.join(param_strs)})"
        if return_type:
            sig += f" -> {return_type}"
        sig += ":"

        is_exported = node.name in exports
        visibility = _determine_visibility(node.name, is_exported)
        doc = _clean_docstring(ast.get_docstring(node))

        start_line = node.lineno
        end_line = getattr(node, "end_lineno", node.lineno)

        # Extract calls made directly in this function body
        callees_raw = self._extract_callees_from_body(node.body)

        body_preview = None
        if config.include_bodies and start_line <= len(source_lines):
            preview_lines = source_lines[start_line - 1 : min(end_line, start_line + 4)]
            body_preview = "\n".join(preview_lines)

        return SymbolInfo(
            id=sym_id,
            name=node.name,
            qualified_name=qname,
            kind=kind,
            file_path=rel_path,
            start_line=start_line,
            end_line=end_line,
            signature=sig,
            parameters=parameters,
            return_type=return_type,
            visibility=visibility,
            modifiers=modifiers,
            docstring=doc,
            parent_id=parent_id,
            children_ids=[],
            inherits=[],
            callees_raw=callees_raw,
            type_refs_raw=sorted(list(set(param_type_refs))),
            body_preview=body_preview,
            is_nested=is_nested,
        )

    def _parse_parameters(self, args: ast.arguments) -> Tuple[List[ParameterInfo], List[str]]:
        """Parse arguments into ParameterInfo list and type reference names."""
        parameters: List[ParameterInfo] = []
        type_refs: List[str] = []

        # Positional args & defaults mapping
        # defaults correspond to the last N args in posonlyargs + args
        all_pos = args.posonlyargs + args.args
        num_pos = len(all_pos)
        num_defaults = len(args.defaults)
        default_offset = num_pos - num_defaults

        for idx, arg in enumerate(all_pos):
            is_posonly = idx < len(args.posonlyargs)
            type_ann = _safe_unparse(arg.annotation)
            if arg.annotation:
                type_refs.extend(_extract_type_names(arg.annotation))

            def_val = None
            is_req = True
            if idx >= default_offset:
                def_val = _safe_unparse(args.defaults[idx - default_offset])
                is_req = False

            parameters.append(
                ParameterInfo(
                    name=arg.arg,
                    type_annotation=type_ann,
                    default_value=def_val,
                    is_required=is_req,
                    kind="positional_only" if is_posonly else "positional_or_keyword",
                )
            )

        # *vararg
        if args.vararg:
            v_type = _safe_unparse(args.vararg.annotation)
            if args.vararg.annotation:
                type_refs.extend(_extract_type_names(args.vararg.annotation))
            parameters.append(
                ParameterInfo(
                    name=args.vararg.arg,
                    type_annotation=v_type,
                    default_value=None,
                    is_required=False,
                    kind="var_positional",
                )
            )

        # Keyword-only args & kw_defaults
        for kwarg, kw_def in zip(args.kwonlyargs, args.kw_defaults):
            kw_type = _safe_unparse(kwarg.annotation)
            if kwarg.annotation:
                type_refs.extend(_extract_type_names(kwarg.annotation))
            def_str = _safe_unparse(kw_def) if kw_def else None
            parameters.append(
                ParameterInfo(
                    name=kwarg.arg,
                    type_annotation=kw_type,
                    default_value=def_str,
                    is_required=def_str is None,
                    kind="keyword_only",
                )
            )

        # **kwarg
        if args.kwarg:
            k_type = _safe_unparse(args.kwarg.annotation)
            if args.kwarg.annotation:
                type_refs.extend(_extract_type_names(args.kwarg.annotation))
            parameters.append(
                ParameterInfo(
                    name=args.kwarg.arg,
                    type_annotation=k_type,
                    default_value=None,
                    is_required=False,
                    kind="var_keyword",
                )
            )

        return parameters, type_refs

    def _extract_callees_from_body(self, body: List[ast.AST]) -> List[str]:
        """
        Extract direct calls within function body, without descending into nested functions/classes.
        """
        callees: List[str] = []

        def visit_stmt(node: ast.AST):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                # Do not recurse into nested symbols
                return

            if isinstance(node, ast.Call):
                func = node.func
                call_str = _safe_unparse(func)
                if call_str:
                    callees.append(call_str)

            for child in ast.iter_child_nodes(node):
                visit_stmt(child)

        for stmt in body:
            visit_stmt(stmt)

        return sorted(list(set(callees)))
