#!/bin/bash

# prod-release/install.sh
# Master installation script for SLSsteam

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd "$SCRIPT_DIR"

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

install_safe_steam_wrapper() {
    local steam_root="$HOME/.local/share/Steam"
    local steam_launcher="$steam_root/steam.sh"
    local steam_backup="$steam_root/steam.sh.bak"
    local wrapper_dir="$HOME/.local/bin"
    local wrapper="$wrapper_dir/steam"
    local native_steam=""

    # Prefer known native Steam launchers and avoid resolving a wrapper that
    # this installer is replacing. Steam can be installed outside /usr/bin.
    local candidate
    for candidate in \
        /usr/bin/steam \
        /usr/local/bin/steam \
        "$HOME/.steam/steam/steam.sh" \
        "$HOME/.local/share/Steam/steam.sh"; do
        if [ -x "$candidate" ] && ! grep -qE 'SLSsteam/library-inject.so|INJECT_SLS|LD_AUDIT' "$candidate" 2>/dev/null; then
            native_steam="$candidate"
            break
        fi
    done

    if [ -z "$native_steam" ]; then
        native_steam="$(type -P steam || true)"
        if [ -n "$native_steam" ] && grep -qE 'SLSsteam/library-inject.so|INJECT_SLS|LD_AUDIT' "$native_steam" 2>/dev/null; then
            native_steam=""
        fi
    fi

    if [ -z "$native_steam" ]; then
        log_error "Native Steam launcher not found; cannot create the SLSsteam wrapper."
        return 1
    fi

    # Headcrab patches steam.sh with a shell-level LD_AUDIT wrapper. That
    # wrapper can terminate Steam before its native client starts. Keep the
    # Steam launcher native and apply the 32-bit audit libraries at the
    # outer command boundary instead.
    if [ -f "$steam_backup" ] && grep -qE 'INJECT_SLS|LD_AUDIT' "$steam_launcher"; then
        if ! grep -qE 'INJECT_SLS|LD_AUDIT' "$steam_backup"; then
            cp -f "$steam_launcher" "$steam_launcher.headcrab-wrapper.bak"
            cp -f "$steam_backup" "$steam_launcher"
            chmod 755 "$steam_launcher"
            log_info "Restored the native Steam launcher."
        fi
    fi

    mkdir -p "$wrapper_dir"
    if [ -e "$wrapper" ] && ! grep -q 'SLSsteam/library-inject.so' "$wrapper" 2>/dev/null; then
        mv -f "$wrapper" "$wrapper.pre-slssteam.bak"
    fi

    cat > "$wrapper" <<EOF
#!/bin/sh
exec env LD_AUDIT="$HOME/.local/share/SLSsteam/library-inject.so:$HOME/.local/share/SLSsteam/SLSsteam.so" "$native_steam" "\$@"
EOF
    chmod 755 "$wrapper"

    # Make the wrapper discoverable from fresh Bash login and interactive
    # shells. Desktop entries still use the explicit LD_AUDIT command above.
    local path_line='export PATH="$HOME/.local/bin:$PATH"'
    for profile in "$HOME/.profile" "$HOME/.bashrc"; do
        touch "$profile"
        if ! grep -Fqx "$path_line" "$profile" 2>/dev/null; then
            printf '\n%s\n' "$path_line" >> "$profile"
        fi
    done

    log_info "Installed the safe SLSsteam Steam wrapper at $wrapper."
}

install_bundled_slssteam() {
    local sls_dir="$SCRIPT_DIR"
    if [ -f "$SCRIPT_DIR/SLSsteam/setup.sh" ]; then
        sls_dir="$SCRIPT_DIR/SLSsteam"
    fi

    if [ ! -f "$sls_dir/setup.sh" ]; then
        log_error "setup.sh not found in the release directory."
        exit 1
    fi

    log_info "Installing the bundled SLSsteam payload..."
    chmod +x "$sls_dir/setup.sh"
    if ! (cd "$sls_dir" && ./setup.sh install); then
        log_error "Bundled SLSsteam installation failed."
        exit 1
    fi
    log_info "Bundled SLSsteam payload installed successfully."
}

log_info "Starting full installation..."

# Source legacy migration helper if available to preserve manifests and clean legacy artifacts
for cand in \
    "$SCRIPT_DIR/scripts/legacy_migration.sh" \
    "$SCRIPT_DIR/legacy_migration.sh"; do
    if [ -f "$cand" ]; then
        # shellcheck source=/dev/null
        source "$cand"
        break
    fi
done

if type migrate_legacy_manifests >/dev/null 2>&1; then
    migrate_legacy_manifests
fi

# 1. Install Headcrab (SLSsteam and Steam client integration)
log_info "Installing Headcrab and SLSsteam..."
HEADCRAB_URL="${SLS_HEADCRAB_URL:-https://raw.githubusercontent.com/Deadboy666/h3adcr-b/main/headcrab.sh}"
HEADCRAB_SCRIPT="$(mktemp "${TMPDIR:-/tmp}/headcrab.XXXXXX.sh")"
trap 'rm -f "$HEADCRAB_SCRIPT"' EXIT

if [[ "$HEADCRAB_URL" == file://* ]]; then
    cp "${HEADCRAB_URL#file://}" "$HEADCRAB_SCRIPT"
elif [ -f "$HEADCRAB_URL" ]; then
    cp "$HEADCRAB_URL" "$HEADCRAB_SCRIPT"
elif command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 --retry-delay 2 "$HEADCRAB_URL" -o "$HEADCRAB_SCRIPT"
elif command -v wget >/dev/null 2>&1; then
    wget -q --tries=3 -O "$HEADCRAB_SCRIPT" "$HEADCRAB_URL"
else
    log_error "Neither curl nor wget is available to download Headcrab."
    exit 1
fi

chmod +x "$HEADCRAB_SCRIPT"
if bash "$HEADCRAB_SCRIPT"; then
    log_info "Headcrab and SLSsteam installed successfully."
else
    log_error "Headcrab/SLSsteam installation failed."
    exit 1
fi

# Headcrab may install its own SLSsteam build. Reinstall the release payload so
# every fresh device uses the tested SLSsteam binary shipped in this archive.
install_bundled_slssteam
if ! install_safe_steam_wrapper; then
    log_error "Steam wrapper installation failed."
    exit 1
fi

# Detect Steam client version and steamclient.so hash
CLIENT_VER=$(grep -oP '"version"\s+"\K[0-9]+' ~/.local/share/Steam/package/steam_client_*.manifest 2>/dev/null | head -1 || true)
CLIENT_SHA=$(sha256sum ~/.local/share/Steam/ubuntu12_32/steamclient.so 2>/dev/null | cut -d' ' -f1 || true)
if [ -n "$CLIENT_VER" ] || [ -n "$CLIENT_SHA" ]; then
    log_info "Steam client ${CLIENT_VER:-unknown} (${CLIENT_SHA:-unknown})"
fi

# Headcrab on SteamOS forces 'SafeMode: yes' in config.yaml.
# Preserve the user's config options while disabling SafeMode so hash differences don't abort startup.
if [ -f "$HOME/.config/SLSsteam/config.yaml" ]; then
    sed -i 's/^SafeMode: .*/SafeMode: no/' "$HOME/.config/SLSsteam/config.yaml"
fi

# Merge updates.yaml safely: union hashes, never overwrite, preserve backups and client hash
SRC_UPDATES=""
if [ -f "$SCRIPT_DIR/res/updates.yaml" ]; then
    SRC_UPDATES="$SCRIPT_DIR/res/updates.yaml"
elif [ -f "$SCRIPT_DIR/SLSsteam/res/updates.yaml" ]; then
    SRC_UPDATES="$SCRIPT_DIR/SLSsteam/res/updates.yaml"
fi

if [ -n "$SRC_UPDATES" ]; then
    mkdir -p "$HOME/.config/SLSsteam"
    DEST_UPDATES="$HOME/.config/SLSsteam/updates.yaml"

    if [ ! -f "$DEST_UPDATES" ]; then
        cp -a "$SRC_UPDATES" "$DEST_UPDATES"
        log_info "Installed initial updates.yaml."
    else
        TS="$(date +%Y%m%d-%H%M%S)"
        cp -a "$DEST_UPDATES" "$DEST_UPDATES.bak-$TS"
        python3 - <<'EOF' "$SRC_UPDATES" "$DEST_UPDATES" "$CLIENT_SHA" "$CLIENT_VER"
import sys, os, re

src_file = sys.argv[1]
dest_file = sys.argv[2]
client_sha = sys.argv[3] if len(sys.argv) > 3 else ""
client_ver = sys.argv[4] if len(sys.argv) > 4 else ""

with open(src_file, 'r', encoding='utf-8') as f:
    src_text = f.read()
with open(dest_file, 'r', encoding='utf-8') as f:
    dest_text = f.read()

dest_hashes = set(re.findall(r'-\s+([0-9a-f]{64})', dest_text))
src_matches = re.findall(r'(\s+-\s+([0-9a-f]{64})(?:[^\n]*))', src_text)

new_lines = []
for line, h in src_matches:
    if h not in dest_hashes:
        new_lines.append(line.rstrip())
        dest_hashes.add(h)

if client_sha and re.match(r'^[0-9a-f]{64}$', client_sha) and client_sha not in dest_hashes:
    new_lines.append(f"    - {client_sha} # local-{client_ver or 'client'}")
    dest_hashes.add(client_sha)

if new_lines:
    beta_pos = dest_text.find("beta-v1.2.0:")
    if beta_pos != -1:
        eol = dest_text.find("\n", beta_pos)
        updated_text = dest_text[:eol+1] + "\n".join(new_lines) + "\n" + dest_text[eol+1:]
    else:
        updated_text = dest_text.rstrip() + "\n" + "\n".join(new_lines) + "\n"
    with open(dest_file, 'w', encoding='utf-8') as f:
        f.write(updated_text)
EOF
        log_info "Safely merged updates.yaml; preserved existing hashes and client hash."
    fi
fi

log_info "Full installation complete!"
echo -e "${GREEN}SLSsteam and Headcrab have been installed to their respective locations in ~/.local/share/${NC}"
