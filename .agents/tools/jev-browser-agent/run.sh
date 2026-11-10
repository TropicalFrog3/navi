#!/usr/bin/env bash
# ==============================================================================
# Helper runner for Jev + Antigravity IDE Hybrid Fast-Browser Agent
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -d "$SCRIPT_DIR/.venv" ]; then
    echo "[!] Virtual environment not found. Running setup.sh first..."
    ./setup.sh
fi

source "$SCRIPT_DIR/.venv/bin/activate"
python -m jev_browser_agent.main "$@"

