"""
Cross-reference resolver, symbol indexer, and call graph builder.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

from .models import FileInfo, ProjectMap, SymbolInfo, SymbolKind

# Common Python/JS/C++ builtins to filter from unresolved warnings
BUILTIN_FUNCTIONS: Set[str] = {
    # Python
    "print", "len", "range", "enumerate", "isinstance", "issubclass", "open",
    "super", "zip", "dict", "list", "set", "tuple", "str", "int", "float",
    "bool", "getattr", "setattr", "hasattr", "sum", "min", "max", "all",
    "any", "sorted", "reversed", "filter", "map", "iter", "next", "repr",
    "type", "id", "hash", "abs", "round", "callable", "vars", "dir",
    # JS/TS
    "console.log", "console.error", "console.warn", "console.info",
    "require", "setTimeout", "clearTimeout", "setInterval", "clearInterval",
    "fetch", "JSON.stringify", "JSON.parse", "Object.keys", "Object.values",
    "Object.assign", "Array.isArray", "Promise.resolve", "Promise.reject",
    # C/C++
    "printf", "malloc", "free", "memset", "memcpy", "sizeof", "strlen",
    "std::min", "std::max", "std::move", "std::forward", "std::make_shared",
    "std::make_unique", "std::cout", "std::cerr", "assert",
}


def resolve_references(
    files: Dict[str, FileInfo],
    symbols: Dict[str, SymbolInfo],
) -> Tuple[
    Dict[str, List[str]],          # call_graph: caller_id -> list of callee_ids
    Dict[str, List[str]],          # reverse_call_graph: callee_id -> list of caller_ids
    List[str],                     # external_dependencies
    List[Dict[str, Any]],          # unresolved_references
    List[str],                     # warnings
]:
    """
    Builds the cross-reference index and resolves calls between symbols.
    Distinguishes direct references from dynamic/uncertain calls.
    """
    warnings: List[str] = []
    unresolved_refs: List[Dict[str, Any]] = []

    # 1. Build lookup tables for symbols
    # id -> symbol
    sym_by_id = symbols

    # (file_path, name) -> list of sym_id
    sym_by_file_and_name: Dict[Tuple[str, str], List[str]] = defaultdict(list)

    # (file_path, qname) -> sym_id
    sym_by_file_and_qname: Dict[Tuple[str, str], str] = {}

    # global name -> list of sym_id
    sym_by_global_name: Dict[str, List[str]] = defaultdict(list)

    # global qname -> list of sym_id
    sym_by_global_qname: Dict[str, List[str]] = defaultdict(list)

    for sym_id, sym in symbols.items():
        sym_by_file_and_name[(sym.file_path, sym.name)].append(sym_id)
        sym_by_file_and_qname[(sym.file_path, sym.qualified_name)] = sym_id
        sym_by_global_name[sym.name].append(sym_id)
        sym_by_global_qname[sym.qualified_name].append(sym_id)

    # 2. Build file import lookup map:
    # file_path -> {imported_name_or_alias: (target_module, original_symbol_name)}
    file_import_map: Dict[str, Dict[str, Tuple[str, str]]] = defaultdict(dict)
    for file_path, file_info in files.items():
        for imp in file_info.imports:
            # e.g. import foo as bar
            for sym_name in imp.imported_symbols:
                local_alias = imp.alias_map.get(sym_name, sym_name)
                file_import_map[file_path][local_alias] = (imp.module, sym_name)
            # e.g. from foo import bar
            if imp.alias_map:
                for orig, alias in imp.alias_map.items():
                    file_import_map[file_path][alias] = (imp.module, orig)

    # 3. Resolve calls for every symbol
    call_graph: Dict[str, List[str]] = defaultdict(list)
    reverse_call_graph: Dict[str, List[str]] = defaultdict(list)

    for caller_id, caller in symbols.items():
        file_path = caller.file_path
        parent_sym = symbols.get(caller.parent_id) if caller.parent_id else None

        for raw_call in caller.callees_raw:
            clean_call = raw_call.strip()
            if not clean_call:
                continue

            # Skip known builtins
            if clean_call in BUILTIN_FUNCTIONS:
                continue

            target_id: Optional[str] = None
            is_direct = False
            is_uncertain = False

            # Case 1: self.<method> or this.<method>
            if clean_call.startswith("self.") or clean_call.startswith("this.") or clean_call.startswith("cls."):
                method_name = clean_call.split(".", 1)[1]
                # Check methods inside caller's class
                class_sym = parent_sym if parent_sym and parent_sym.kind in (SymbolKind.CLASS, SymbolKind.STRUCT) else None
                if class_sym:
                    method_qname = f"{class_sym.qualified_name}.{method_name}"
                    if (file_path, method_qname) in sym_by_file_and_qname:
                        target_id = sym_by_file_and_qname[(file_path, method_qname)]
                        is_direct = True
                    else:
                        # Check inherited classes
                        for base_name in class_sym.inherits:
                            clean_base = base_name.split(".")[-1]
                            base_candidates = sym_by_global_name.get(clean_base, [])
                            for base_id in base_candidates:
                                base_sym = symbols.get(base_id)
                                if base_sym:
                                    base_m_qname = f"{base_sym.qualified_name}.{method_name}"
                                    if (base_sym.file_path, base_m_qname) in sym_by_file_and_qname:
                                        target_id = sym_by_file_and_qname[(base_sym.file_path, base_m_qname)]
                                        is_direct = True
                                        break
                            if target_id:
                                break

            # Case 2: Direct call to symbol defined in same file
            if not target_id:
                if (file_path, clean_call) in sym_by_file_and_qname:
                    target_id = sym_by_file_and_qname[(file_path, clean_call)]
                    is_direct = True
                elif (file_path, clean_call) in sym_by_file_and_name:
                    cands = sym_by_file_and_name[(file_path, clean_call)]
                    if len(cands) == 1:
                        target_id = cands[0]
                        is_direct = True

            # Case 3: Call to imported symbol
            if not target_id:
                # Check if clean_call or its prefix is in file imports
                parts = clean_call.split(".")
                first_part = parts[0]
                if first_part in file_import_map[file_path]:
                    target_mod, orig_sym = file_import_map[file_path][first_part]
                    # Find file corresponding to target_mod
                    norm_mod_path = target_mod.replace(".", "/")
                    target_files = [
                        f for f in files
                        if f.startswith(norm_mod_path) or f == f"{norm_mod_path}.py"
                    ]
                    if target_files:
                        t_file = target_files[0]
                        target_sym_name = parts[1] if len(parts) > 1 else orig_sym
                        cands = sym_by_file_and_name.get((t_file, target_sym_name), [])
                        if cands:
                            target_id = cands[0]
                            is_direct = True

            # Case 4: Attribute call (obj.method) - dynamic / uncertain
            if not target_id and "." in clean_call:
                method_name = clean_call.split(".")[-1]
                candidates = sym_by_global_name.get(method_name, [])
                if candidates:
                    # Match found across project, but receiver object is dynamically typed
                    target_id = candidates[0]
                    is_uncertain = True

            # Case 5: Project-wide unique symbol
            if not target_id and clean_call in sym_by_global_name:
                cands = sym_by_global_name[clean_call]
                if len(cands) == 1:
                    target_id = cands[0]
                    is_direct = True
                elif len(cands) > 1:
                    target_id = cands[0]
                    is_uncertain = True

            # Register resolved reference
            if target_id:
                if is_direct:
                    if target_id not in caller.resolved_callees:
                        caller.resolved_callees.append(target_id)
                    if caller_id not in symbols[target_id].resolved_callers:
                        symbols[target_id].resolved_callers.append(caller_id)
                    call_graph[caller_id].append(target_id)
                    reverse_call_graph[target_id].append(caller_id)
                elif is_uncertain:
                    if target_id not in caller.uncertain_callees:
                        caller.uncertain_callees.append(target_id)
            else:
                # Unresolved call
                # Don't complain about common lowercase single identifiers like int(), str(), etc.
                if len(clean_call) > 1 and not clean_call.startswith("_"):
                    unresolved_refs.append({
                        "caller_id": caller_id,
                        "callee_name": clean_call,
                        "file_path": file_path,
                        "line": caller.start_line,
                    })

    # Sort all resolved relationships deterministically
    for sym in symbols.values():
        sym.resolved_callees.sort()
        sym.resolved_callers.sort()
        sym.uncertain_callees.sort()

    for k in list(call_graph.keys()):
        call_graph[k] = sorted(list(set(call_graph[k])))
    for k in list(reverse_call_graph.keys()):
        reverse_call_graph[k] = sorted(list(set(reverse_call_graph[k])))

    # 4. Extract external dependencies
    ext_deps_set: Set[str] = set()
    for file_info in files.values():
        for imp in file_info.imports:
            if not imp.is_internal:
                # Normalize dependency name
                top_pkg = imp.module.split(".")[0].strip("\"'<>")
                if top_pkg:
                    ext_deps_set.add(top_pkg)

    external_dependencies = sorted(list(ext_deps_set))

    return call_graph, reverse_call_graph, external_dependencies, unresolved_refs, warnings
