#!/usr/bin/env python3
"""
Project Map Generator: Live, agent-readable codebase structure mapper.

Usage:
    python3 scripts/project_map.py generate [options]
    ./scripts/project_map.py generate --root . --output PROJECT_MAP.md
"""

import os
import sys
from pathlib import Path

# Add repo root and scripts directory to sys.path so project_map module is importable
CURRENT_DIR = Path(__file__).resolve().parent
REPO_ROOT = CURRENT_DIR.parent

if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
if str(CURRENT_DIR) not in sys.path:
    sys.path.insert(0, str(CURRENT_DIR))

try:
    from scripts.project_map.cli import main
except ImportError:
    from project_map.cli import main

if __name__ == "__main__":
    sys.exit(main())
