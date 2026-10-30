#!/bin/bash
set -euo pipefail

# 1. Check for bubblewrap (bwrap)
if ! command -v bwrap >/dev/null 2>&1; then
    echo "SKIP: bubblewrap (bwrap) not found in PATH"
    exit 77
fi

# 2. Check for g++ -m32 multilib compiler support
if ! command -v g++ >/dev/null 2>&1; then
    echo "SKIP: g++ not found in PATH"
    exit 77
fi

if ! echo "int main(){}" | g++ -m32 -x c++ - -o /dev/null 2>/dev/null; then
    echo "SKIP: g++ does not support 32-bit multilib (-m32)"
    exit 77
fi

# 3. Discover 32-bit steamclient.so
STEAMCLIENT_PATH=""
CANDIDATES=(
    "${STEAMCLIENT_SO:-}"
    "${STEAM32_DIR:-}/steamclient.so"
    "${HOME}/.local/share/Steam/ubuntu12_32/steamclient.so"
    "${HOME}/.steam/steam/ubuntu12_32/steamclient.so"
)

for cand in "${CANDIDATES[@]}"; do
    if [ -n "$cand" ] && [ -f "$cand" ]; then
        STEAMCLIENT_PATH="$cand"
        break
    fi
done

if [ -z "$STEAMCLIENT_PATH" ]; then
    echo "SKIP: 32-bit steamclient.so not found in standard Steam runtime locations"
    exit 77
fi

STEAM32_DIR="$(cd "$(dirname "$STEAMCLIENT_PATH")" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ ! -f "$REPO_ROOT/res/cloud_redirect.so" ]; then
    echo "ERROR: res/cloud_redirect.so not found in repository at $REPO_ROOT" >&2
    exit 1
fi

if [ ! -f "$REPO_ROOT/tests/integration/gate07_harness.cpp" ]; then
    echo "ERROR: tests/integration/gate07_harness.cpp not found" >&2
    exit 1
fi

echo "=== Gate 0.7 Bubblewrap Hermetic Test ==="
echo "Repo root:      $REPO_ROOT"
echo "Steam32 dir:    $STEAM32_DIR"
echo "steamclient.so: $STEAMCLIENT_PATH"

bwrap \
  --ro-bind / / \
  --tmpfs /home \
  --tmpfs /tmp \
  --proc /proc \
  --dev /dev \
  --ro-bind "$REPO_ROOT" /mnt \
  --dir /home/testuser \
  --dir /home/testuser/steam32 \
  --ro-bind "$STEAM32_DIR" /home/testuser/steam32 \
  --setenv HOME /home/testuser \
  --setenv GATE07_SANDBOX 1 \
  --chdir /home/testuser \
  -- /bin/bash -c '
    set -euo pipefail

    mkdir -p /home/testuser/.config/SLSsteam /home/testuser/.config/CloudRedirect
    mkdir -p /home/testuser/.local/share/Steam/config

    cat <<EOF > /home/testuser/.config/SLSsteam/config.yaml
# >>> NAVI CLOUD SHIM (DO NOT EDIT MANUALLY)
DisableCloud: no
AdditionalApps:
  - 480
# <<< NAVI CLOUD SHIM
EOF

    cat <<EOF > /home/testuser/.local/share/Steam/config/loginusers.vdf
"users"
{
    "76561198000000001"
    {
        "AccountName" "testuser"
        "PersonaName" "testuser"
        "MostRecent"  "1"
        "Timestamp"   "1700000000"
    }
}
EOF

    echo "Compiling 32-bit test harness inside sandbox..."
    g++ -m32 /mnt/tests/integration/gate07_harness.cpp -L/home/testuser/steam32 -l:steamclient.so -ldl -o /home/testuser/steam

    export LD_LIBRARY_PATH="/usr/lib/i386-linux-gnu:/lib/i386-linux-gnu:/home/testuser/steam32:/home/testuser/steam32/steam-runtime/usr/lib/i386-linux-gnu:/home/testuser/steam32/steam-runtime/lib/i386-linux-gnu"
    echo "Executing Gate 0.7 harness under LD_PRELOAD=/mnt/res/cloud_redirect.so..."

    RC=0
    LD_PRELOAD=/mnt/res/cloud_redirect.so /home/testuser/steam --steamclient /home/testuser/steam32/steamclient.so || RC=$?

    if [ $RC -ne 0 ]; then
        echo "=== Gate 0.7 test harness FAILED with code $RC ==="
        echo "=== cloud_redirect.log ==="
        cat /home/testuser/.config/CloudRedirect/cloud_redirect.log 2>/dev/null || true
        echo "=== cr_debug.log ==="
        cat /home/testuser/.config/CloudRedirect/cr_debug.log 2>/dev/null || true
        exit $RC
    fi
'
