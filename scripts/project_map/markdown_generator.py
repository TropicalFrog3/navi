"""
Deterministic, agent-readable Markdown generator for codebase structure maps.
"""

from __future__ import annotations

import re
from typing import Dict, List, Set

from .models import FileInfo, ProjectMap, SymbolInfo, SymbolKind, Visibility


def slugify_anchor(raw_str: str) -> str:
    """Create a safe, deterministic Markdown anchor slug."""
    slug = re.sub(r'[^a-zA-Z0-9_\-]+', '-', raw_str)
    return slug.strip('-').lower()


def format_visibility_badge(vis: Visibility | str) -> str:
    val = vis.value if isinstance(vis, Visibility) else str(vis)
    if val == "public":
        return "`+` public"
    elif val == "private":
        return "`-` private"
    elif val == "protected":
        return "`#` protected"
    elif val == "internal":
        return "`~` internal"
    elif val == "exported":
        return "`*` exported"
    return f"`{val}`"


def generate_markdown(project_map: ProjectMap) -> str:
    """
    Renders the complete deterministic Markdown document for AI agent consumption.
    """
    lines: List[str] = []

    # 1. Header & Metadata
    lines.append(f"# Project Map: {project_map.project_name}\n")
    lines.append("> Live, deterministic codebase map optimized for AI agents and developer tooling.\n")

    lines.append("## Generation Metadata\n")
    lines.append(f"- **Generation Command:** `{project_map.generation_command}`")
    lines.append(f"- **Project Root:** `{project_map.root_path}`")
    inc_str = ", ".join([f"`{p}`" for p in project_map.config.include_patterns]) if project_map.config.include_patterns else "*All eligible source files*"
    exc_str = ", ".join([f"`{p}`" for p in project_map.config.exclude_patterns]) if project_map.config.exclude_patterns else "*Default build/vendor ignores*"
    lines.append(f"- **Scope (Include):** {inc_str}")
    lines.append(f"- **Exclusions:** {exc_str}")
    if project_map.config.max_depth is not None:
        lines.append(f"- **Max Depth:** `{project_map.config.max_depth}`")
    langs_str = ", ".join([f"`{l}`" for l in project_map.languages]) if project_map.languages else "auto-detected"
    lines.append(f"- **Languages Analyzed:** {langs_str}")
    lines.append(f"- **Private Symbols:** `{'Included' if project_map.config.include_private else 'Excluded (public/exported only)'}`")
    lines.append(f"- **Bodies:** `{'Included (concise preview)' if project_map.config.include_bodies else 'Signatures only (bodies excluded)'}`")
    if project_map.config.include_timestamps:
        import datetime
        lines.append(f"- **Generated At:** `{datetime.datetime.now(datetime.timezone.utc).isoformat()}`")
    lines.append("")

    # 2. Legend
    lines.append("## Legend & Notation\n")
    lines.append("| Notation | Category | Meaning |")
    lines.append("| :--- | :--- | :--- |")
    lines.append("| `+` | Visibility | Public symbol |")
    lines.append("| `-` | Visibility | Private symbol |")
    lines.append("| `#` | Visibility | Protected symbol |")
    lines.append("| `~` | Visibility | Package-private / Internal symbol |")
    lines.append("| `*` | Visibility | Explicitly exported symbol (`export` / `__all__`) |")
    lines.append("| `-->` | Relationship | Direct call or reference |")
    lines.append("| `-.->` | Relationship | Dynamic / Uncertain call reference |")
    lines.append("| `<|--` | Relationship | Inheritance / Class extension |")
    lines.append("| `..>` | Relationship | Type reference / Parameter type usage |")
    lines.append("| `[L12-L34]` | Location | Source file line range |")
    lines.append("")

    # 3. High-Level Architecture Overview
    lines.append("## High-Level Architecture Overview\n")
    stats = project_map.stats
    lines.append("### Project Metrics\n")
    lines.append("| Metric | Value |")
    lines.append("| :--- | :--- |")
    lines.append(f"| Scanned Source Files | {stats.get('total_files', len(project_map.files))} |")
    lines.append(f"| Total Source Lines | {stats.get('total_lines', 0)} |")
    lines.append(f"| Total Symbols Extracted | {stats.get('total_symbols', len(project_map.symbols))} |")
    lines.append(f"| Classes & Structs | {stats.get('classes_count', 0)} |")
    lines.append(f"| Functions & Methods | {stats.get('functions_count', 0)} |")
    lines.append(f"| External Dependencies | {len(project_map.external_dependencies)} |")
    lines.append(f"| Resolved Direct References | {stats.get('resolved_references_count', 0)} |")
    lines.append(f"| Dynamic / Uncertain References | {stats.get('uncertain_references_count', 0)} |")
    lines.append("")

    if project_map.external_dependencies:
        lines.append("### External Dependencies\n")
        lines.append(", ".join([f"`{dep}`" for dep in sorted(project_map.external_dependencies)]))
        lines.append("\n")

    # High Fan-Out & Central Symbols
    top_called = sorted(
        [s for s in project_map.symbols.values() if s.resolved_callers],
        key=lambda s: len(s.resolved_callers),
        reverse=True,
    )[:10]

    top_calling = sorted(
        [s for s in project_map.symbols.values() if s.resolved_callees],
        key=lambda s: len(s.resolved_callees),
        reverse=True,
    )[:10]

    if top_called or top_calling:
        lines.append("### Central & High-Fan-Out Symbols\n")
        lines.append("| Symbol | Kind | Inbound Callers | Outbound Calls | Location |")
        lines.append("| :--- | :--- | :--- | :--- | :--- |")
        central_ids = {s.id for s in top_called} | {s.id for s in top_calling}
        sorted_central = sorted(
            [project_map.symbols[sid] for sid in central_ids],
            key=lambda s: (len(s.resolved_callers) + len(s.resolved_callees)),
            reverse=True,
        )[:12]
        for s in sorted_central:
            s_anchor = f"#sym-{slugify_anchor(s.id)}"
            f_link = f"[{s.file_path}#L{s.start_line}-L{s.end_line}](#file-{slugify_anchor(s.file_path)})"
            lines.append(f"| [`{s.qualified_name}`]({s_anchor}) | `{s.kind.value}` | {len(s.resolved_callers)} | {len(s.resolved_callees)} | {f_link} |")
        lines.append("")

    # Key Entry Points & Public APIs
    entry_points = [
        s for s in project_map.symbols.values()
        if s.name in ("main", "run", "cli", "app") or s.kind == SymbolKind.ROUTE or s.visibility == Visibility.EXPORTED
    ]
    if entry_points:
        lines.append("### Key Entry Points & Public APIs\n")
        lines.append("| Entry Point | Kind | Signature | File |")
        lines.append("| :--- | :--- | :--- | :--- |")
        for ep in sorted(entry_points, key=lambda s: (s.file_path, s.start_line))[:15]:
            ep_anchor = f"#sym-{slugify_anchor(ep.id)}"
            lines.append(f"| [`{ep.qualified_name}`]({ep_anchor}) | `{ep.kind.value}` | `{ep.signature[:60]}` | `{ep.file_path}:{ep.start_line}` |")
        lines.append("")

    # 4. Project Skeleton & Directory Tree
    lines.append("## Directory Tree & File Skeleton\n")
    lines.append("```text")
    lines.append(project_map.tree_text.strip())
    lines.append("```\n")

    lines.append("### File Summary Table\n")
    lines.append("| File Path | Module | Language | Lines | Symbols | Structural Summary |")
    lines.append("| :--- | :--- | :--- | :--- | :--- | :--- |")
    for file_path in sorted(project_map.files.keys()):
        fi = project_map.files[file_path]
        file_anchor = f"#file-{slugify_anchor(file_path)}"
        lines.append(f"| [{file_path}]({file_anchor}) | `{fi.module_name}` | `{fi.language}` | {fi.line_count} | {len(fi.symbol_ids)} | {fi.structural_summary} |")
    lines.append("")

    # 5. Global Symbol Index Table
    lines.append("## Symbol Index\n")
    lines.append("| Symbol | Kind | Visibility | File Location | Parent Scope |")
    lines.append("| :--- | :--- | :--- | :--- | :--- |")
    for sym_id in sorted(project_map.symbols.keys()):
        s = project_map.symbols[sym_id]
        sym_anchor = f"#sym-{slugify_anchor(s.id)}"
        loc_str = f"[{s.file_path}:L{s.start_line}-L{s.end_line}](#file-{slugify_anchor(s.file_path)})"
        parent_str = f"`{project_map.symbols[s.parent_id].qualified_name}`" if (s.parent_id and s.parent_id in project_map.symbols) else "*(root)*"
        vis_badge = format_visibility_badge(s.visibility)
        lines.append(f"| [`{s.qualified_name}`]({sym_anchor}) | `{s.kind.value}` | {vis_badge} | {loc_str} | {parent_str} |")
    lines.append("")

    # 6. Call & Reference Graph
    lines.append("## Call & Reference Graph\n")

    # Mermaid diagram of major inter-symbol calls
    edges: List[str] = []
    # Collect direct edges
    for caller_id, callees in sorted(project_map.call_graph.items()):
        caller = project_map.symbols.get(caller_id)
        if not caller:
            continue
        c_node = f"{slugify_anchor(caller.name)}[\"{caller.qualified_name}\"]"
        for callee_id in callees:
            callee = project_map.symbols.get(callee_id)
            if not callee:
                continue
            # avoid cluttering diagram if caller and callee are identical
            if caller_id == callee_id:
                continue
            e_node = f"{slugify_anchor(callee.name)}[\"{callee.qualified_name}\"]"
            edges.append(f"    {c_node} --> {e_node}")

    if edges:
        lines.append("### Architecture Call Flow (Mermaid)\n")
        lines.append("```mermaid")
        lines.append("graph TD")
        # Deduplicate and sort edges, limit to top 30 for diagram clarity
        unique_edges = sorted(list(set(edges)))[:30]
        lines.extend(unique_edges)
        lines.append("```\n")

    lines.append("### Cross-Reference Table\n")
    lines.append("| Caller Symbol | Status | Callee / Target Symbol | Type References |")
    lines.append("| :--- | :--- | :--- | :--- |")
    has_refs = False
    for sym_id in sorted(project_map.symbols.keys()):
        s = project_map.symbols[sym_id]
        caller_link = f"[`{s.qualified_name}`](#sym-{slugify_anchor(s.id)})"
        type_str = ", ".join([f"`{t}`" for t in s.type_refs_raw[:4]]) if s.type_refs_raw else "-"

        # Direct callees
        for callee_id in s.resolved_callees:
            callee = project_map.symbols.get(callee_id)
            if callee:
                has_refs = True
                callee_link = f"[`{callee.qualified_name}`](#sym-{slugify_anchor(callee.id)})"
                lines.append(f"| {caller_link} | `--> direct` | {callee_link} | {type_str} |")

        # Uncertain callees
        for unc_id in s.uncertain_callees:
            unc = project_map.symbols.get(unc_id)
            if unc:
                has_refs = True
                unc_link = f"[`{unc.qualified_name}`](#sym-{slugify_anchor(unc.id)})"
                lines.append(f"| {caller_link} | `-.-> uncertain (dynamic)` | {unc_link} | {type_str} |")

    if not has_refs:
        lines.append("| *(none)* | - | *(no internal cross references detected)* | - |")
    lines.append("")

    # 7. Per-File Detailed Sections
    lines.append("## Per-File Detailed Structure\n")
    for file_path in sorted(project_map.files.keys()):
        fi = project_map.files[file_path]
        if not fi.symbol_ids and not fi.imports:
            continue
        file_anchor_id = f"file-{slugify_anchor(file_path)}"
        lines.append(f"### <a id=\"{file_anchor_id}\"></a>File: `{file_path}`\n")
        lines.append(f"- **Module:** `{fi.module_name}` | **Language:** `{fi.language}` | **Lines:** {fi.line_count} (`L1-L{fi.line_count}`)")
        if fi.docstring:
            lines.append(f"- **Summary:** {fi.docstring}")
        lines.append(f"- **Structure:** {fi.structural_summary}")

        # Imports
        if fi.imports:
            internal_imps = [i for i in fi.imports if i.is_internal]
            external_imps = [i for i in fi.imports if not i.is_internal]
            if internal_imps:
                lines.append("- **Internal Imports:** " + ", ".join([f"`{i.module}`" for i in internal_imps]))
            if external_imps:
                lines.append("- **External Dependencies:** " + ", ".join([f"`{i.module}`" for i in external_imps]))

        # Exports
        if fi.exports:
            lines.append("- **Exports:** " + ", ".join([f"`{e}`" for e in fi.exports]))

        lines.append("")

        # Symbols in this file
        file_symbols = [project_map.symbols[sid] for sid in fi.symbol_ids if sid in project_map.symbols]
        # Only render root symbols first, children nested under their parents
        root_symbols = [s for s in file_symbols if not s.parent_id or s.parent_id not in project_map.symbols]

        if root_symbols:
            for rs in sorted(root_symbols, key=lambda s: s.start_line):
                _render_symbol_details(rs, project_map, lines, indent_level=4)
        else:
            lines.append("*No declarations in this file.*\n")

    return "\n".join(lines)


def _render_symbol_details(
    s: SymbolInfo,
    project_map: ProjectMap,
    lines: List[str],
    indent_level: int = 4,
):
    """Render details for a symbol and its nested children recursively."""
    sym_anchor_id = f"sym-{slugify_anchor(s.id)}"
    h_prefix = "#" * indent_level

    lines.append(f"{h_prefix} <a id=\"{sym_anchor_id}\"></a>`{s.name}` ({s.kind.value})\n")
    lines.append(f"- **Qualified Name:** `{s.qualified_name}`")
    lines.append(f"- **Signature:** `{s.signature}`")
    lines.append(f"- **Visibility:** {format_visibility_badge(s.visibility)}")
    if s.modifiers:
        lines.append(f"- **Modifiers / Decorators:** " + ", ".join([f"`{m}`" for m in s.modifiers]))
    if s.return_type:
        lines.append(f"- **Return Type:** `{s.return_type}`")
    if s.inherits:
        lines.append(f"- **Inherits / Implements:** " + ", ".join([f"`{b}`" for b in s.inherits]))
    if s.docstring:
        lines.append(f"- **Docstring:** *{s.docstring}*")
    lines.append(f"- **Source Location:** [`{s.file_path}:L{s.start_line}-L{s.end_line}`](#file-{slugify_anchor(s.file_path)})")

    # Parameters
    if s.parameters:
        lines.append("- **Parameters:**")
        lines.append("  | Name | Type | Default | Required | Kind |")
        lines.append("  | :--- | :--- | :--- | :--- | :--- |")
        for p in s.parameters:
            t_str = f"`{p.type_annotation}`" if p.type_annotation else "*(any)*"
            d_str = f"`{p.default_value}`" if p.default_value is not None else "*(none)*"
            r_str = "Yes" if p.is_required else "No"
            lines.append(f"  | `{p.name}` | {t_str} | {d_str} | {r_str} | `{p.kind}` |")

    # Callees
    if s.resolved_callees:
        callee_links = []
        for cid in s.resolved_callees:
            c = project_map.symbols.get(cid)
            if c:
                callee_links.append(f"[`{c.qualified_name}`](#sym-{slugify_anchor(c.id)})")
        if callee_links:
            lines.append(f"- **Calls:** {', '.join(callee_links)}")

    # Callers
    if s.resolved_callers:
        caller_links = []
        for cid in s.resolved_callers:
            c = project_map.symbols.get(cid)
            if c:
                caller_links.append(f"[`{c.qualified_name}`](#sym-{slugify_anchor(c.id)})")
        if caller_links:
            lines.append(f"- **Called By:** {', '.join(caller_links)}")

    # Uncertain calls
    if s.uncertain_callees:
        unc_links = []
        for cid in s.uncertain_callees:
            c = project_map.symbols.get(cid)
            if c:
                unc_links.append(f"[`{c.qualified_name}` (dynamic)](#sym-{slugify_anchor(c.id)})")
        if unc_links:
            lines.append(f"- **Uncertain / Dynamic Calls:** {', '.join(unc_links)}")

    # Body preview if requested
    if s.body_preview:
        lines.append("- **Body Preview:**")
        lines.append("  ```")
        for bline in s.body_preview.splitlines():
            lines.append(f"  {bline}")
        lines.append("  ```")

    lines.append("")

    # Render children recursively
    child_symbols = [
        project_map.symbols[cid] for cid in s.children_ids
        if cid in project_map.symbols
    ]
    for child in sorted(child_symbols, key=lambda c: c.start_line):
        _render_symbol_details(child, project_map, lines, indent_level=min(6, indent_level + 1))
