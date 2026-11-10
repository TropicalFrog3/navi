#!/usr/bin/env bash
# ==============================================================================
# Setup script for Jev + Antigravity IDE Hybrid Fast-Browser Agent
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================================================"
echo " Setting up Jev + Antigravity IDE Hybrid Fast-Browser Agent"
echo "========================================================================"

# 1. Check Python version
PYTHON_BIN=""
for cmd in python3.12 python3.11 python3.10 python3 python; do
    if command -v "$cmd" &>/dev/null; then
        PYTHON_VERSION=$("$cmd" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
        MAJOR=$(echo "$PYTHON_VERSION" | cut -d. -f1)
        MINOR=$(echo "$PYTHON_VERSION" | cut -d. -f2)
        if [ "$MAJOR" -eq 3 ] && [ "$MINOR" -ge 10 ]; then
            PYTHON_BIN="$cmd"
            echo "[+] Detected compatible Python: $cmd (v$PYTHON_VERSION)"
            break
        fi
    fi
done

if [ -z "$PYTHON_BIN" ]; then
    echo "[-] Error: Python 3.10 or higher is required."
    exit 1
fi

# 2. Virtual environment setup
VENV_DIR="$SCRIPT_DIR/.venv"
if [ ! -d "$VENV_DIR" ]; then
    echo "[+] Creating virtual environment in $VENV_DIR..."
    "$PYTHON_BIN" -m venv "$VENV_DIR"
else
    echo "[+] Virtual environment exists at $VENV_DIR"
fi

# 3. Upgrade pip and install dependencies
VENV_PYTHON="$VENV_DIR/bin/python"
VENV_PIP="$VENV_DIR/bin/pip"

echo "[+] Upgrading pip..."
"$VENV_PIP" install --upgrade pip

echo "[+] Installing requirements from requirements.txt..."
"$VENV_PIP" install -r requirements.txt

# 4. Install Playwright browser binaries
echo "[+] Installing Playwright Chromium browser..."
"$VENV_DIR/bin/playwright" install chromium

# 5. Environment configuration (.env)
if [ ! -f "$SCRIPT_DIR/.env" ]; then
    echo "[+] Creating .env from .env.example..."
    cp "$SCRIPT_DIR/.env.example" "$SCRIPT_DIR/.env"
    echo "[!] Notice: Please set your TYPESAFE_API_KEY in $SCRIPT_DIR/.env (No Gemini API key needed when using Antigravity IDE!)."
else
    echo "[+] Existing .env file found."
fi

# 6. Ensure runner scripts are executable
chmod +x "$SCRIPT_DIR/setup.sh" || true
if [ -f "$SCRIPT_DIR/run.sh" ]; then
    chmod +x "$SCRIPT_DIR/run.sh" || true
fi

echo "========================================================================"
echo " Setup complete!"
echo ""
echo " To run the agent:"
echo "   source .venv/bin/activate"
echo "   python -m jev_browser_agent.main --url https://news.ycombinator.com --goal 'Find the top story and read comments'"
echo " Or run tests:"
echo "   pytest tests/ -v"
echo "========================================================================"
