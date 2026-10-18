#!/usr/bin/env bash
# ==============================================================================
# deck-ctl.sh: Master Wireless Controller CLI for Physical Steam Deck (SteamOS)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Target configuration: can be overridden via DECK_IP / DECK_HOST environment variable
# or stored in ~/.config/deck-ctl.conf or .deck-host in repo root.
CONFIG_FILE="${HOME}/.config/deck-ctl.conf"
REPO_HOST_FILE="${REPO_DIR}/.deck-host"

if [ -n "${DECK_HOST:-}" ]; then
    TARGET_HOST="$DECK_HOST"
elif [ -n "${DECK_IP:-}" ]; then
    TARGET_HOST="$DECK_IP"
elif [ -f "$REPO_HOST_FILE" ]; then
    TARGET_HOST="$(tr -d ' \n\r' < "$REPO_HOST_FILE")"
elif [ -f "$CONFIG_FILE" ]; then
    TARGET_HOST="$(tr -d ' \n\r' < "$CONFIG_FILE")"
else
    TARGET_HOST="steamdeck.local"
fi

TARGET_USER="deck"
TARGET_PORT=22

SSH_OPTS=(-p "$TARGET_PORT" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -o LogLevel=ERROR)

usage() {
    cat << EOF
Steam Deck Wireless Host Controller CLI
Current Target: ${TARGET_USER}@${TARGET_HOST}

Usage: $(basename "$0") <command> [arguments...]

Target Management:
  set-host <ip|hostname>     Save persistent Steam Deck IP or hostname
  status                     Comprehensive status: Wi-Fi, battery, mode, Steam, CEF

Connection & Shell:
  ssh                        Open interactive shell into Steam Deck
  exec "<command>"           Execute command inside Steam Deck as user deck
  sudo "<command>"           Execute command with sudo on Steam Deck
  tunnel                     Establish background SSH tunnel for CEF port 8080 (PC -> Deck)
  tunnel-status              Check if CEF port 8080 SSH tunnel is active
  tunnel-stop                Terminate CEF port 8080 SSH tunnel

File Synchronization & Deployment:
  sync                       Rsync workspace repository to ~/navi on Deck
  deploy                     Sync repo, run ./install.sh on Deck, and restart Steam
  verify                     Verify Steam Deck installation, log health, and feature status
  test [all|flow]            Execute full test suite or UI flow test on the Steam Deck
  copy-to <src> <dest>       SCP copy file/folder to Steam Deck
  copy-from <src> <dest>     SCP copy file/folder from Steam Deck

Steam & UI Automation:
  restart-steam              Restart Steam (detects Gaming Mode vs Desktop Mode)
  steam-url "<steam://...>"  Invoke Steam URL protocol inside Steam Deck
  notify "<title>" "<body>"  Show notification popup on Steam Deck screen
  cdp-targets                List active Steam CEF targets (via port 8080/8081)
  cdp-eval "<target>" "<js>" Evaluate JavaScript expression on target via CDP WebSocket

Monitoring & Diagnostics:
  tail-sls                   Stream ~/.navi.log in real time
  tail-content               Stream Steam's content_log.txt in real time
  tail-gamescope             Stream Gaming Mode compositor journal logs

EOF
    exit 1
}

cmd_set_host() {
    local host="$1"
    if [ -z "$host" ]; then
        echo "Error: Hostname or IP required. Example: $(basename "$0") set-host 192.168.40.113"
        exit 1
    fi
    echo "$host" > "$REPO_HOST_FILE"
    mkdir -p "$(dirname "$CONFIG_FILE")"
    echo "$host" > "$CONFIG_FILE"
    echo "[✓] Target Steam Deck host saved as: $host"
}

cmd_status() {
    echo "=========================================================="
    echo " Steam Deck Wireless Status Check (${TARGET_HOST})"
    echo "=========================================================="

    # 1. Ping / Reachability
    if ping -c 1 -W 2 "$TARGET_HOST" > /dev/null 2>&1; then
        local ping_time
        ping_time=$(ping -c 1 -W 2 "$TARGET_HOST" 2>/dev/null | awk -F'/' 'END{print $5}')
        echo " [✓] Network Reachability : ONLINE (RTT: ~${ping_time}ms)"
    else
        echo " [✗] Network Reachability : OFFLINE or ping blocked ($TARGET_HOST)"
    fi

    # 2. SSH Authentication
    if ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" "echo OK" 2>/dev/null | grep -q "OK"; then
        echo " [✓] SSH Authentication   : SUCCESS ($TARGET_USER@$TARGET_HOST:22)"

        # Query System Telemetry
        local sys_info
        sys_info=$(ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" 'bash -s' << 'EOF'
            KERNEL=$(uname -r)
            BAT_CAP=$(cat /sys/class/power_supply/BAT1/capacity 2>/dev/null || cat /sys/class/power_supply/BAT0/capacity 2>/dev/null || echo "N/A")
            BAT_STAT=$(cat /sys/class/power_supply/BAT1/status 2>/dev/null || cat /sys/class/power_supply/BAT0/status 2>/dev/null || echo "N/A")
            MODE="Unknown"
            if pgrep gamescope >/dev/null 2>&1; then
                MODE="Gaming Mode (Gamescope)"
            elif pgrep plasmashell >/dev/null 2>&1; then
                MODE="Desktop Mode (KDE Plasma)"
            fi
            STEAM_PID=$(pgrep -f "steamwebhelper" | head -n1 || true)
            echo "$KERNEL|$BAT_CAP|$BAT_STAT|$MODE|$STEAM_PID"
EOF
        )
        IFS='|' read -r k_ver bat_cap bat_stat deck_mode steam_pid <<< "$sys_info"
        echo " [i] Operating Mode       : $deck_mode"
        echo " [i] Kernel               : $k_ver"
        echo " [i] Battery              : ${bat_cap}% (${bat_stat})"

        if [ -n "$steam_pid" ]; then
            echo " [✓] Steam Process        : RUNNING (PID: $steam_pid)"
        else
            echo " [!] Steam Process        : NOT DETECTED"
        fi

        # Check repository sync state on Deck
        local repo_status
        repo_status=$(ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" \
          "[ -d ~/navi ] && echo 'EXISTS' || echo 'MISSING'")
        if [ "$repo_status" = "EXISTS" ]; then
            echo " [✓] Workspace Sync       : ~/navi exists on Deck"
        else
            echo " [!] Workspace Sync       : ~/navi not found (run: $(basename "$0") sync)"
        fi
    else
        echo " [✗] SSH Authentication   : FAILED ($TARGET_USER@$TARGET_HOST)"
        echo "     (Ensure you ran: ssh-copy-id deck@$TARGET_HOST)"
        echo "=========================================================="
        return 1
    fi

    # 3. Check CEF Remote Debugging
    # Check direct Wi-Fi port 8081
    local cdp_active=0
    if curl -s -m 2 "http://${TARGET_HOST}:8081/json" > /dev/null 2>&1; then
        local target_count
        target_count=$(curl -s "http://${TARGET_HOST}:8081/json" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))" 2>/dev/null || echo "0")
        echo " [✓] CEF Remote (Wi-Fi)   : ACTIVE on http://${TARGET_HOST}:8081 ($target_count targets)"
        cdp_active=1
    else
        echo " [!] CEF Remote (Wi-Fi)   : Port 8081 not responding on LAN"
    fi

    # Check local port 8080 (tunnel)
    if curl -s -m 2 "http://127.0.0.1:8080/json" > /dev/null 2>&1; then
        local tunnel_count
        tunnel_count=$(curl -s "http://127.0.0.1:8080/json" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))" 2>/dev/null || echo "0")
        echo " [✓] CEF Local Tunnel     : ACTIVE on http://127.0.0.1:8080 ($tunnel_count targets)"
    else
        echo " [i] CEF Local Tunnel     : INACTIVE on 127.0.0.1:8080 (run: $(basename "$0") tunnel)"
    fi

    echo "=========================================================="
}

cmd_ssh() {
    exec ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST"
}

cmd_exec() {
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" "$@"
}

cmd_sudo() {
    ssh -t "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" "sudo $@"
}

cmd_sync() {
    echo "=========================================================="
    echo " Syncing workspace to Steam Deck: ~/navi"
    echo "=========================================================="
    rsync -avz --delete-after \
      --exclude '.git' \
      --exclude '.agents' \
      --exclude '.vscode' \
      --exclude '.claude' \
      --exclude 'steam-os/' \
      --exclude 'navi.zip' \
      --exclude '.venv' \
      --exclude 'venv' \
      --exclude '__pycache__' \
      --exclude '*.pyc' \
      --exclude '*.ppm' \
      --exclude '*.log' \
      -e "ssh ${SSH_OPTS[*]}" \
      "$REPO_DIR/" "$TARGET_USER@$TARGET_HOST:~/navi/"
    echo " [✓] Workspace sync complete!"
}

cmd_deploy() {
    cmd_sync
    echo ""
    echo "=========================================================="
    echo " Executing ./install.sh on Steam Deck..."
    echo "=========================================================="
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" 'bash -s' << 'EOF'
cd ~/navi
chmod +x ./install.sh ./uninstall.sh 2>/dev/null || true
./install.sh install --yes
EOF
    echo " [✓] Installation script executed on Steam Deck."
    echo ""
    read -p "Restart Steam now to load updated Navi hooks? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cmd_restart_steam
    fi
}

cmd_copy_to() {
    local src="$1"
    local dest="$2"
    scp "${SSH_OPTS[@]}" -r "$src" "$TARGET_USER@$TARGET_HOST:$dest"
}

cmd_copy_from() {
    local src="$1"
    local dest="$2"
    scp "${SSH_OPTS[@]}" -r "$TARGET_USER@$TARGET_HOST:$src" "$dest"
}

cmd_tunnel() {
    echo "Checking CEF SSH tunnel to Steam Deck..."
    if lsof -Pi :8080 -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo " [✓] Port 8080 is already listening on host PC."
    else
        ssh "${SSH_OPTS[@]}" -f -N -L 8080:127.0.0.1:8080 "$TARGET_USER@$TARGET_HOST"
        sleep 1
        if lsof -Pi :8080 -sTCP:LISTEN -t >/dev/null 2>&1; then
            echo " [✓] Background SSH tunnel established: 127.0.0.1:8080 -> Steam Deck:8080"
        else
            echo " [✗] Failed to establish tunnel."
            return 1
        fi
    fi
}

cmd_tunnel_status() {
    if lsof -Pi :8080 -sTCP:LISTEN -t >/dev/null 2>&1; then
        local pid
        pid=$(lsof -Pi :8080 -sTCP:LISTEN -t | head -n1)
        echo " [✓] CEF Tunnel ACTIVE on port 8080 (PID: $pid)"
    else
        echo " [!] No tunnel listening on local port 8080."
    fi
}

cmd_tunnel_stop() {
    local pids
    pids=$(lsof -Pi :8080 -sTCP:LISTEN -t 2>/dev/null || true)
    if [ -n "$pids" ]; then
        kill $pids 2>/dev/null || true
        echo " [✓] Closed tunnel process(es): $pids"
    else
        echo " [i] No process listening on port 8080."
    fi
}

cmd_restart_steam() {
    echo "=========================================================="
    echo " Restarting Steam on Steam Deck..."
    echo "=========================================================="
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" 'bash -s' << 'EOF'
# Ensure remote debugging flag is in place
mkdir -p ~/.steam/steam
touch ~/.steam/steam/.cef-enable-remote-debugging

if pgrep gamescope >/dev/null 2>&1; then
    echo "Detected Gaming Mode (Gamescope session)."
    echo "Signaling Steam process to restart under Gamescope supervisor..."
    pkill -TERM -f steamwebhelper 2>/dev/null || true
    pkill -TERM steam 2>/dev/null || true
    echo "Gamescope will automatically respawn Steam."
else
    echo "Detected Desktop Mode (KDE Plasma)."
    pkill -9 -f steamwebhelper 2>/dev/null || true
    pkill -9 steam 2>/dev/null || true
    sleep 1
    export DISPLAY=:0
    export WAYLAND_DISPLAY=wayland-0
    export XDG_RUNTIME_DIR=/run/user/1000
    export PATH="/home/deck/.local/bin:$PATH"
    if command -v kstart5 >/dev/null 2>&1; then
        kstart5 -- /home/deck/.local/bin/steam -cef-enable-debugging > /dev/null 2>&1
    elif command -v kstart >/dev/null 2>&1; then
        kstart -- /home/deck/.local/bin/steam -cef-enable-debugging > /dev/null 2>&1
    else
        nohup /home/deck/.local/bin/steam -cef-enable-debugging > /home/deck/steam-debug.log 2>&1 &
    fi
    echo "Steam launched in Desktop Mode with Navi wrapper."
fi
EOF
}

cmd_steam_url() {
    local url="$1"
    echo "Sending steam URL protocol to Steam Deck: $url"
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" \
      "DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 steam '$url'"
}

cmd_notify() {
    local title="$1"
    local msg="${2:-}"
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" \
      "DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 notify-send '$title' '$msg'"
}

get_cdp_url() {
    if curl -s -m 1 "http://${TARGET_HOST}:8081/json" >/dev/null 2>&1; then
        echo "http://${TARGET_HOST}:8081"
    elif curl -s -m 1 "http://127.0.0.1:8080/json" >/dev/null 2>&1; then
        echo "http://127.0.0.1:8080"
    else
        cmd_tunnel >/dev/null 2>&1 || true
        echo "http://127.0.0.1:8080"
    fi
}

cmd_cdp_targets() {
    local cdp_base
    cdp_base=$(get_cdp_url)
    python3 - << EOF
import urllib.request, json, sys

base = "$cdp_base"
try:
    with urllib.request.urlopen(f"{base}/json", timeout=3) as resp:
        targets = json.loads(resp.read().decode())
    print(f"Active Steam CEF Targets via {base}:")
    print("-" * 60)
    for t in targets:
        title = t.get("title", "<No Title>")
        url = t.get("url", "")
        ws = t.get("webSocketDebuggerUrl", "")
        print(f" * {title}")
        if url:
            print(f"   URL: {url[:80]}")
    print("-" * 60)
    print(f"Total: {len(targets)} targets")
except Exception as e:
    print(f"Failed to query CEF targets: {e}")
    sys.exit(1)
EOF
}

cmd_cdp_eval() {
    local target_title="$1"
    local expr="$2"
    local cdp_base
    cdp_base=$(get_cdp_url)
    python3 - << EOF
import json, urllib.request, websocket, sys

base = "$cdp_base"
target = "$target_title"
expr = """$expr"""

try:
    req = urllib.request.Request(f"{base}/json")
    with urllib.request.urlopen(req, timeout=3) as r:
        pages = json.loads(r.read().decode())
except Exception as e:
    print(f"Error querying {base}/json: {e}")
    sys.exit(1)

ws_url = None
for p in pages:
    if target.lower() in p.get("title", "").lower():
        ws_url = p.get("webSocketDebuggerUrl")
        break

if not ws_url:
    print(f"Target matching '{target}' not found. Available targets:")
    for p in pages:
        print(f" - {p.get('title')}")
    sys.exit(1)

try:
    ws = websocket.create_connection(ws_url, timeout=5)
    ws.send(json.dumps({
        "id": 1,
        "method": "Runtime.evaluate",
        "params": {
            "expression": expr,
            "returnByValue": True,
            "awaitPromise": True,
            "userGesture": True
        }
    }))
    res = json.loads(ws.recv())
    ws.close()
    val = res.get("result", {}).get("result", {}).get("value")
    if isinstance(val, (dict, list)):
        print(json.dumps(val, indent=2))
    else:
        print(val)
except Exception as e:
    print(f"CDP evaluation error: {e}")
    sys.exit(1)
EOF
}

cmd_tail_sls() {
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" "tail -n 50 -f ~/.navi.log"
}

cmd_tail_content() {
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" "tail -n 50 -f ~/.local/share/Steam/logs/content_log.txt"
}

cmd_tail_gamescope() {
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" "journalctl --user -fu gamescope-session-plus"
}

cmd_test() {
    local suite="${1:-all}"
    cmd_sync
    echo ""
    echo "=========================================================="
    echo " Running Tests on Steam Deck (${TARGET_HOST})..."
    echo "=========================================================="
    if [ "$suite" = "flow" ] || [ "$suite" = "ui" ]; then
        ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" '~/deck_venv/bin/python ~/navi/tests/test_steam_ui_flow.py'
    else
        ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" 'cd ~/navi && ./tests/run_test_suite.sh'
    fi
}

cmd_verify() {
    echo "=========================================================="
    echo " Verifying Steam Deck Navi Installation & Health          "
    echo "=========================================================="

    # 1. Reachability
    if ! ping -c 1 -W 2 "$TARGET_HOST" > /dev/null 2>&1; then
        echo " [✗] Cannot reach Steam Deck at $TARGET_HOST"
        exit 1
    fi

    # 2. Client version and steamclient hash
    echo " [1/3] Checking Steam Client Version & Hash..."
    local ver_info
    ver_info=$(ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" '
        ver=$(grep -oP "\"version\"\s+\"\K[0-9]+" ~/.local/share/Steam/package/steam_client_*.manifest 2>/dev/null | head -1)
        sha=$(sha256sum ~/.local/share/Steam/ubuntu12_32/steamclient.so 2>/dev/null | cut -d" " -f1)
        echo "Version: ${ver:-unknown}"
        echo "Hash:    ${sha:-unknown}"
    ')
    echo "$ver_info"

    # 3. Check ~/.navi.log for Errors, Aborting, not found!
    echo ""
    echo " [2/3] Checking ~/.navi.log for Fatal Errors / Aborts..."
    local error_lines
    error_lines=$(ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" '
        if [ ! -f ~/.navi.log ]; then
            echo "MISSING_LOG"
            exit 0
        fi
        grep -E "\[Error |\[Notify.*Error|Aborting|not found\!" ~/.navi.log 2>/dev/null || true
    ')

    if [ "$error_lines" = "MISSING_LOG" ]; then
        echo " [✗] Error: ~/.navi.log does not exist on target!"
        exit 1
    elif [ -n "$error_lines" ]; then
        echo " [✗] Found errors in ~/.navi.log:"
        echo "$error_lines"
        exit 1
    else
        echo " [✓] Zero Error/Aborting/not found! lines in ~/.navi.log"
    fi

    # 4. Feature confirmations
    echo ""
    echo " [3/3] Confirming Active Features in Log..."
    ssh "${SSH_OPTS[@]}" "$TARGET_USER@$TARGET_HOST" '
        log=~/.navi.log
        if [ ! -f "$log" ]; then exit 1; fi

        # Hooks installed
        if grep -q "Loaded successfully" "$log" 2>/dev/null || grep -q "Core hooks setup" "$log" 2>/dev/null || grep -q "Hooks::setup" "$log" 2>/dev/null; then
            echo " [✓] Core Hooks Installed & Active"
        else
            echo " [?] Core Hooks installation confirmation not found in log"
        fi

        # CDP Inject
        if grep -q "CDP" "$log" 2>/dev/null || grep -q "SharedJSContext" "$log" 2>/dev/null; then
            echo " [✓] CDP Injection Active"
        else
            echo " [?] CDP Injection not yet recorded in log"
        fi

        # StoreInject server
        if grep -q "StoreInject" "$log" 2>/dev/null; then
            echo " [✓] StoreInject Server Active"
        else
            echo " [?] StoreInject not yet recorded in log"
        fi

        # Compat tool fallback or native
        if grep -q "Compat fallback" "$log" 2>/dev/null || grep -q "native vfunc absent, using fallback" "$log" 2>/dev/null; then
            echo " [✓] Compat Tool Fallback Active & Operational"
        elif grep -q "16IClientCompatMap" "$log" 2>/dev/null; then
            echo " [✓] Compat Interface Loaded"
        fi
    '

    echo ""
    echo "=========================================================="
    echo " [✓] Verification Complete: All checks passed!"
    echo "=========================================================="
}

# Main dispatcher
ACTION="${1:-}"
shift || true

case "$ACTION" in
    set-host) cmd_set_host "$@" ;;
    status) cmd_status ;;
    ssh) cmd_ssh ;;
    exec) cmd_exec "$@" ;;
    sudo) cmd_sudo "$@" ;;
    sync) cmd_sync ;;
    deploy) cmd_deploy ;;
    verify) cmd_verify "$@" ;;
    test) cmd_test "$@" ;;
    copy-to) cmd_copy_to "$@" ;;
    copy-from) cmd_copy_from "$@" ;;
    tunnel) cmd_tunnel ;;
    tunnel-status) cmd_tunnel_status ;;
    tunnel-stop) cmd_tunnel_stop ;;
    restart-steam) cmd_restart_steam ;;
    steam-url) cmd_steam_url "$@" ;;
    notify) cmd_notify "$@" ;;
    cdp-targets) cmd_cdp_targets ;;
    cdp-eval) cmd_cdp_eval "$@" ;;
    tail-sls) cmd_tail_sls ;;
    tail-content) cmd_tail_content ;;
    tail-gamescope) cmd_tail_gamescope ;;
    *) usage ;;
esac
