#!/usr/bin/env python3
"""
@file  bundle-app-details.py
@brief Bundles modular app-details JS and HTML into monolithic inject script.

Usage:
    python3 tools/bundle-app-details.py          # Bundle and write output
    python3 tools/bundle-app-details.py --check  # Verify sync, exit 1 if drift
"""

from __future__ import annotations

import argparse
import difflib
import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "library" / "app-details"
DEFAULT_OUTPUT_FILE = REPO_ROOT / "res" / "inject-scripts" / "library" / "app-details-script.js"

TEMPLATE_FILES = [
    ("GAME_MANAGER_MODAL_HTML", "game-manager-modal.html"),
    ("PROVIDER_MODAL_HTML", "provider-modal.html"),
    ("REMOVE_MODAL_HTML", "remove-modal.html"),
    ("RESTORE_MODAL_HTML", "restore-modal.html"),
    ("RESET_MODAL_HTML", "reset-modal.html"),
    ("NAVI_MODAL_HTML", "navi-modal.html"),
]

JS_MODULE_FILES = [
    "gamepad.js",
    "appid.js",
    "cloud-api.js",
    "game-manager-modal.js",
    "provider-modal.js",
    "remove-modal.js",
    "restore-modal.js",
    "reset-modal.js",
    "navi-modal.js",
    "buttons.js",
    "main.js",
]


def indent_code(code: str, spaces: int = 4) -> str:
    """Indent lines by a given number of spaces, preserving empty lines."""
    pad = " " * spaces
    lines = code.splitlines()
    indented = [f"{pad}{line}" if line.strip() else "" for line in lines]
    return "\n".join(indented)


def build_bundle(modules_dir: Path) -> str:
    """Assemble all modular CSS, HTML, and JS files into a single JS string."""
    # 1. Read CSS
    css_path = modules_dir / "styles.css"
    if not css_path.is_file():
        raise FileNotFoundError(f"Missing styles file: {css_path}")
    css_content = css_path.read_text(encoding="utf-8")

    # 2. Read HTML templates
    templates: dict[str, str] = {}
    for var_name, filename in TEMPLATE_FILES:
        tpl_path = modules_dir / filename
        if not tpl_path.is_file():
            raise FileNotFoundError(f"Missing HTML template: {tpl_path}")
        templates[var_name] = tpl_path.read_text(encoding="utf-8")

    # 3. Read JS modules
    js_modules: list[tuple[str, str]] = []
    for filename in JS_MODULE_FILES:
        mod_path = modules_dir / filename
        if not mod_path.is_file():
            raise FileNotFoundError(f"Missing JS module: {mod_path}")
        js_modules.append((filename, mod_path.read_text(encoding="utf-8")))

    # 4. Assemble bundle
    parts: list[str] = [
        "/**",
        " * @file  app-details-script.js",
        " * @brief Bundled standalone script for Steam library game details page",
        " */",
        "(function() {",
        "    'use strict';",
        "    if (window.__slsAppDetailsInjected) return;",
        "    window.__slsAppDetailsInjected = true;",
        "",
        "    function log(m) {",
        "        console.log('[SLS] ' + m);",
        "        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function(){});",
        "    }",
        "",
        "    // Inlined Styles & Templates",
        f"    var SLS_STYLES = {json.dumps(css_content)};",
    ]

    for var_name, _ in TEMPLATE_FILES:
        parts.append(f"    var {var_name} = {json.dumps(templates[var_name])};")

    parts.extend([
        "",
        "    function ensureStyles() {",
        "        if (document.getElementById('sls-app-details-styles')) return;",
        "        var style = document.createElement('style');",
        "        style.id = 'sls-app-details-styles';",
        "        style.textContent = SLS_STYLES;",
        "        var target = document.head || document.body || document.documentElement;",
        "        if (target) target.appendChild(style);",
        "    }",
    ])

    for filename, code in js_modules:
        parts.append(f"\n    // --- {filename} ---")
        parts.append(indent_code(code, spaces=4))

    parts.append("})();\n")

    return "\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description="Bundle modular SLS App Details files into app-details-script.js")
    parser.add_argument("--check", action="store_true", help="Check whether the bundled output is in sync with modules")
    parser.add_argument("--modules-dir", type=Path, default=DEFAULT_MODULES_DIR, help="Path to modular directory")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_FILE, help="Path to output bundle file")

    args = parser.parse_args()

    modules_dir = args.modules_dir.resolve()
    output_file = args.output.resolve()

    if not modules_dir.is_dir():
        print(f"Error: Modules directory not found: {modules_dir}", file=sys.stderr)
        return 1

    try:
        bundled = build_bundle(modules_dir)
    except Exception as e:
        print(f"Error building bundle: {e}", file=sys.stderr)
        return 1

    if args.check:
        if not output_file.is_file():
            print(f"Error: Target bundle file does not exist: {output_file}", file=sys.stderr)
            return 1
        current = output_file.read_text(encoding="utf-8")
        if current != bundled:
            print(f"ERROR: Drift detected between modular files and {output_file}!", file=sys.stderr)
            diff = difflib.unified_diff(
                current.splitlines(keepends=True),
                bundled.splitlines(keepends=True),
                fromfile=str(output_file),
                tofile="bundled (generated)",
                n=3,
            )
            sys.stderr.writelines(diff)
            print("\nRun 'python3 tools/bundle-app-details.py' to regenerate the bundle.", file=sys.stderr)
            return 1
        print(f"OK: {output_file} is in sync with modular sources.")
        return 0

    # Write output
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(bundled, encoding="utf-8")
    print(f"Bundled 18 modular files into {output_file}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

