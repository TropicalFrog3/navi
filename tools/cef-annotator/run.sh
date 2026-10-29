#!/usr/bin/env bash
# ==============================================================================
# Steam CEF Annotator - Unified Launcher & CLI
# ==============================================================================
set -e

SCRIPT_PATH="$(readlink -f "${BASH_SOURCE[0]}")"
DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"
PYTHON="${PYTHON:-python3}"
VIBE_URL="http://127.0.0.1:3846/health"
CDP_URL="http://127.0.0.1:8080/json/version"

cmd="${1:-run}"

case "$cmd" in
  start|run)
    echo "========================================"
    echo "       Steam CEF Annotator Launcher     "
    echo "========================================"
    
    # 1. Check Steam Remote Debugging (port 8080)
    if ! curl -s --max-time 1 "$CDP_URL" >/dev/null 2>&1; then
      echo "⚠️  [Notice] Steam debugging port 8080 not yet detected."
      echo "   Start Steam with: steam -cef-enable-debugging"
      echo "   (The annotator daemon will wait and auto-attach as soon as Steam opens)"
      echo ""
    else
      echo "✅ Steam debugging port 8080 reachable."
    fi

    # 2. Check / Auto-start Vibe Annotations Server
    if ! curl -s --max-time 1 "$VIBE_URL" >/dev/null 2>&1; then
      echo "🚀 Starting Vibe Annotations Server (port 3846)..."
      npx vibe-annotations-server start >/dev/null 2>&1 &
      for i in {1..20}; do
        if curl -s --max-time 0.5 "$VIBE_URL" >/dev/null 2>&1; then
          echo "✅ Vibe Server ready on http://127.0.0.1:3846"
          break
        fi
        sleep 0.2
      done
    else
      echo "✅ Vibe Annotations Server is already running."
    fi

    echo "🚀 Starting Annotator Daemon..."
    echo "   (Press Ctrl+C at any time to stop)"
    echo "----------------------------------------"
    exec "$PYTHON" "$DIR/annotator.py" run "${@:2}"
    ;;

  toggle)
    exec "$PYTHON" "$DIR/annotator.py" toggle
    ;;

  status)
    exec "$PYTHON" "$DIR/annotator.py" status
    ;;

  clear)
    exec "$PYTHON" "$DIR/annotator.py" clear
    ;;

  list)
    exec "$PYTHON" "$DIR/annotator.py" list
    ;;

  stop)
    echo "Stopping any running annotator daemon processes..."
    if pkill -f "annotator.py run" 2>/dev/null; then
      echo "✅ Annotator daemon stopped."
    else
      echo "ℹ️  No running annotator daemon found."
    fi
    ;;

  help|-h|--help)
    echo "Usage: steam-annotator [command]"
    echo ""
    echo "Commands:"
    echo "  (default / run)   Start Vibe server and annotator daemon (auto-attaches to Steam)"
    echo "  toggle            Toggle in-client annotate mode in Steam (or press Alt+Shift+A)"
    echo "  status            Check status of Steam port 8080, Vibe server, and local store"
    echo "  list              List all saved annotations"
    echo "  clear             Clear all saved annotations"
    echo "  stop              Stop running annotator daemon"
    echo "  help              Show this help message"
    ;;

  *)
    exec "$PYTHON" "$DIR/annotator.py" "$@"
    ;;
esac
