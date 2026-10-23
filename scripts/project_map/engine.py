"""
Core execution engine for scanning, analyzing, resolving, and rendering project maps.
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Set, Tuple

from .adapters import get_adapter_for_file
from .json_generator import generate_json
from .markdown_generator import generate_markdown
from .models import FileInfo, ParseConfig, ProjectMap, SymbolInfo, SymbolKind
from .resolver import resolve_references
from .scanner import build_directory_tree, scan_project_files


def run_project_map(config: ParseConfig) -> Tuple[ProjectMap, str]:
    """
    Executes the full pipeline: scan -> parse -> resolve -> format.
    Returns the ProjectMap object and the formatted output string.
    """
    root = config.root_path.resolve()
    project_name = root.name

    # 1. Scan files
    scanned_files = scan_project_files(config)

    # 2. Build directory tree
    tree_text = build_directory_tree(root, scanned_files, config.max_depth)

    # 3. Parse files with language adapters
    files_map: Dict[str, FileInfo] = {}
    symbols_map: Dict[str, SymbolInfo] = {}
    all_warnings: List[str] = []
    languages_seen: Set[str] = set()

    for full_path, rel_path in scanned_files:
        try:
            content = full_path.read_text(encoding="utf-8", errors="replace")
        except Exception as e:
            all_warnings.append(f"Failed to read file {rel_path}: {e}")
            continue

        adapter = get_adapter_for_file(full_path, config.language)
        if adapter:
            languages_seen.add(adapter.language_id)
            f_info, syms, warns = adapter.parse(full_path, rel_path, content, config)
            all_warnings.extend(warns)
            files_map[rel_path] = f_info
            for s in syms:
                symbols_map[s.id] = s
        else:
            # Generic file fallback
            lines = content.splitlines()
            f_info = FileInfo(
                path=rel_path,
                module_name=Path(rel_path).stem,
                language="generic",
                line_count=len(lines),
                size_bytes=len(content.encode("utf-8")),
                docstring=None,
                imports=[],
                exports=[],
                symbol_ids=[],
                structural_summary=f"{len(lines)} lines (no parser registered)",
            )
            files_map[rel_path] = f_info

    # 4. Resolve cross-references and build call graph
    call_graph, reverse_call_graph, ext_deps, unresolved_refs, resolver_warns = resolve_references(
        files_map, symbols_map
    )
    all_warnings.extend(resolver_warns)

    # 5. Compute stats
    resolved_count = sum(len(s.resolved_callees) for s in symbols_map.values())
    uncertain_count = sum(len(s.uncertain_callees) for s in symbols_map.values())
    classes_count = sum(1 for s in symbols_map.values() if s.kind in (SymbolKind.CLASS, SymbolKind.STRUCT, SymbolKind.INTERFACE))
    functions_count = sum(1 for s in symbols_map.values() if s.kind in (SymbolKind.FUNCTION, SymbolKind.METHOD, SymbolKind.NESTED_FUNCTION))
    total_lines = sum(f.line_count for f in files_map.values())

    stats = {
        "total_files": len(files_map),
        "total_lines": total_lines,
        "total_symbols": len(symbols_map),
        "classes_count": classes_count,
        "functions_count": functions_count,
        "resolved_references_count": resolved_count,
        "uncertain_references_count": uncertain_count,
        "unresolved_count": len(unresolved_refs),
    }

    # 6. Assemble ProjectMap
    project_map = ProjectMap(
        project_name=project_name,
        root_path=str(root),
        generation_command=config.generation_command,
        config=config,
        languages=sorted(list(languages_seen)),
        files=files_map,
        symbols=symbols_map,
        tree_text=tree_text,
        call_graph=call_graph,
        reverse_call_graph=reverse_call_graph,
        external_dependencies=ext_deps,
        unresolved_references=unresolved_refs,
        warnings=all_warnings,
        stats=stats,
    )

    # 7. Render output
    if config.output_format.lower() == "json":
        output_content = generate_json(project_map)
    else:
        output_content = generate_markdown(project_map)

    # Ensure parent output directory exists
    config.output_path.parent.mkdir(parents=True, exist_ok=True)
    config.output_path.write_text(output_content, encoding="utf-8")

    return project_map, output_content
