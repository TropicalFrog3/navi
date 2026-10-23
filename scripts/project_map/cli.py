"""
Command Line Interface for the Project Map Generator.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional

from .engine import run_project_map
from .models import ParseConfig
from .watcher import watch_and_regenerate


def print_summary(project_map, output_path: Path) -> None:
    """Print clean execution summary to stdout."""
    stats = project_map.stats
    print("=" * 60)
    print(" Project Code Map Generation Summary")
    print("=" * 60)
    print(f" Project Name:          {project_map.project_name}")
    print(f" Project Root:          {project_map.root_path}")
    print(f" Files Scanned:         {stats.get('total_files', 0)}")
    print(f" Total Code Lines:      {stats.get('total_lines', 0)}")
    print(f" Symbols Extracted:     {stats.get('total_symbols', 0)}")
    print(f"   - Classes/Structs:   {stats.get('classes_count', 0)}")
    print(f"   - Functions/Methods: {stats.get('functions_count', 0)}")
    print(f" References Resolved:   {stats.get('resolved_references_count', 0)} (direct)")
    print(f" Dynamic/Uncertain:     {stats.get('uncertain_references_count', 0)}")
    print(f" Unresolved Call Names: {stats.get('unresolved_count', 0)}")
    print(f" Languages Detected:    {', '.join(project_map.languages) if project_map.languages else 'none'}")
    print(f" Output Format:         {project_map.config.output_format}")
    print(f" Output Written To:     {output_path.resolve()}")
    if project_map.warnings:
        print(f" Warnings:              {len(project_map.warnings)}")
        for w in project_map.warnings[:5]:
            print(f"   [!] {w}")
        if len(project_map.warnings) > 5:
            print(f"   ... and {len(project_map.warnings) - 5} more warnings.")
    else:
        print(" Warnings:              0")
    print("=" * 60)


def build_parser() -> argparse.ArgumentParser:
    """Constructs the command line argument parser."""
    parser = argparse.ArgumentParser(
        prog="project_map",
        description="Generates a live, deterministic, agent-readable codebase structure map.",
    )

    subparsers = parser.add_subparsers(dest="subcommand", help="Available subcommands")

    # generate subcommand
    gen_parser = subparsers.add_parser("generate", help="Create or refresh the codebase map")
    _add_generate_args(gen_parser)

    # Also add generate arguments to the top-level parser so running without subcommand works
    _add_generate_args(parser)

    return parser


def _add_generate_args(p: argparse.ArgumentParser) -> None:
    """Add arguments common to generate subcommand and default command."""
    p.add_argument(
        "--root",
        "-r",
        type=Path,
        default=Path("."),
        help="Project root directory (default: current directory)",
    )
    p.add_argument(
        "--output",
        "-o",
        type=Path,
        default=Path("PROJECT_MAP.md"),
        help="Path for generated output file (default: PROJECT_MAP.md)",
    )
    p.add_argument(
        "--include",
        action="append",
        dest="include_patterns",
        default=[],
        help="Include glob pattern (can be specified multiple times, e.g. --include 'src/**')",
    )
    p.add_argument(
        "--exclude",
        action="append",
        dest="exclude_patterns",
        default=[],
        help="Exclude glob pattern (can be specified multiple times, e.g. --exclude 'tests/fixtures/**')",
    )
    p.add_argument(
        "--language",
        type=str,
        default=None,
        help="Force or restrict to a specific language parser (e.g. python, cpp, javascript, typescript, all)",
    )
    p.add_argument(
        "--max-depth",
        type=int,
        default=None,
        help="Limit directory traversal and skeleton tree depth",
    )
    p.add_argument(
        "--include-private",
        action="store_true",
        default=False,
        help="Include private and protected internal symbols in output map",
    )
    p.add_argument(
        "--include-bodies",
        action="store_true",
        default=False,
        help="Include short function body previews in detailed sections",
    )
    p.add_argument(
        "--format",
        dest="output_format",
        choices=["markdown", "json"],
        default="markdown",
        help="Output format: 'markdown' for AI agents, 'json' for tooling (default: markdown)",
    )
    p.add_argument(
        "--watch",
        action="store_true",
        default=False,
        help="Continuously watch source files and automatically regenerate upon changes",
    )
    p.add_argument(
        "--include-timestamps",
        action="store_true",
        default=False,
        help="Include volatile generation timestamp in metadata (omitted by default for determinism)",
    )


def parse_args_to_config(argv: Optional[List[str]] = None) -> ParseConfig:
    """Parse CLI arguments into ParseConfig."""
    parser = build_parser()
    args = parser.parse_args(argv)

    cmd_str = "project_map"
    if len(sys.argv) > 1:
        cmd_str += " " + " ".join(sys.argv[1:])
    else:
        cmd_str += " generate"

    return ParseConfig(
        root_path=args.root,
        output_path=args.output,
        include_patterns=args.include_patterns or [],
        exclude_patterns=args.exclude_patterns or [],
        language=args.language,
        max_depth=args.max_depth,
        include_private=args.include_private,
        include_bodies=args.include_bodies,
        output_format=args.output_format,
        watch=args.watch,
        include_timestamps=args.include_timestamps,
        generation_command=cmd_str,
    )


def main(argv: Optional[List[str]] = None) -> int:
    """Main CLI entrypoint."""
    config = parse_args_to_config(argv)

    def do_generate(cfg: ParseConfig) -> None:
        project_map, _ = run_project_map(cfg)
        print_summary(project_map, cfg.output_path)

    if config.watch:
        do_generate(config)
        watch_and_regenerate(config, do_generate)
        return 0
    else:
        do_generate(config)
        return 0


if __name__ == "__main__":
    sys.exit(main())
