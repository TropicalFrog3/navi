#!/usr/bin/env bash
#
# Launcher for Navi UI Live Preview & Hot Reload Dev Server.
# Usage:
#   ./start.sh [--port 5050] [--steam] [--fixture store] ...
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 is required to run the live preview server." >&2
    exit 1
fi

exec python3 "$SCRIPT_DIR/server.py" "$@"

