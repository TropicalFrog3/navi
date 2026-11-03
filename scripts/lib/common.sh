#!/usr/bin/env bash
# scripts/lib/common.sh - Shared utilities and definitions for Navi
set -u

# Terminal color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default variables
REPO_ROOT="${REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ASSUME_YES="${ASSUME_YES:-0}"
DRY_RUN="${DRY_RUN:-0}"

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_dry() {
    echo -e "[DRY-RUN] $1"
}

has_zenity() {
    [ "${ASSUME_YES:-0}" -ne 1 ] && \
    [ "${SLS_TEST_ENV:-0}" -ne 1 ] && \
    [ -t 0 ] && \
    command -v zenity >/dev/null 2>&1 && \
    [ -n "${DISPLAY:-${WAYLAND_DISPLAY:-}}" ]
}

find_native_steam() {
    local candidate
    for candidate in \
        "$HOME/.local/share/Steam/steam.sh" \
        "$HOME/.steam/steam/steam.sh" \
        /usr/games/steam \
        /usr/bin/steam \
        /usr/local/bin/steam; do
        if [ -x "$candidate" ] && ! grep -qE 'navi/library-inject|navi/libnavi|LD_AUDIT=' "$candidate" 2>/dev/null; then
            echo "$candidate"
            return 0
        fi
    done
    local in_path
    in_path="$(command -v steam 2>/dev/null || true)"
    if [ -n "$in_path" ] && [ -x "$in_path" ] && [ "$in_path" != "$HOME/.local/bin/steam" ] && ! grep -qE 'navi/library-inject|navi/libnavi|LD_AUDIT=' "$in_path" 2>/dev/null; then
        echo "$in_path"
        return 0
    fi
    echo "/usr/games/steam"
    return 0
}

check_steam_running() {
    local running=0
    if [ "${NAVI_FAKE_STEAM_RUNNING:-0}" = "1" ]; then
        running=1
    elif pgrep -x steam >/dev/null 2>&1 || pgrep -f 'steam\.sh' >/dev/null 2>&1 || pgrep -x steamwebhelper >/dev/null 2>&1; then
        running=1
    fi

    if [ "$running" -eq 1 ]; then
        if [ "${DRY_RUN:-0}" -eq 1 ]; then
            log_dry "Steam process detected; check bypassed in dry-run mode"
            return 0
        fi

        if [ "${NAVI_IGNORE_RUNNING_STEAM:-0}" = "1" ]; then
            log_warn "Steam is currently running, but NAVI_IGNORE_RUNNING_STEAM is set. Continuing..."
            return 0
        fi

        # Non-interactive mode: fail with clear error
        if [ "${ASSUME_YES:-0}" -eq 1 ] || [ "${SLS_TEST_ENV:-0}" = "1" ] || [ ! -t 0 ]; then
            log_error "Steam is currently running. Please close Steam before proceeding."
            return 1
        fi

        # Interactive mode: prompt
        if has_zenity; then
            if ! zenity --question \
                --title="Steam is Running" \
                --text="Steam is currently running. Please close Steam before proceeding.\n\nContinue anyway?" \
                --ok-label="Continue" --cancel-label="Cancel" 2>/dev/null; then
                log_error "Operation cancelled by user because Steam is running."
                return 1
            fi
        else
            echo -e "${YELLOW}[WARN] Steam is currently running.${NC}"
            read -r -p "Steam is currently running. Close Steam before proceeding, or continue anyway? [y/N]: " response
            case "$response" in
                [yY][eE][sS]|[yY])
                    return 0
                    ;;
                *)
                    log_error "Operation cancelled: Steam is currently running."
                    return 1
                    ;;
            esac
        fi
    else
        if [ "${DRY_RUN:-0}" -eq 1 ]; then
            log_dry "Steam process check: Steam is not running"
        fi
    fi
    return 0
}

smart_copy() {
    local src_dir="$1"
    local dest_dir="$2"
    if [ ! -d "$src_dir" ]; then
        return 0
    fi
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Copy $src_dir/* to $dest_dir/"
        return 0
    fi
    mkdir -p "$dest_dir"
    for src_path in "$src_dir"/*; do
        [ -e "$src_path" ] || continue
        local item_name="$(basename "$src_path")"
        local dest_path="$dest_dir/$item_name"
        if [ -d "$src_path" ]; then
            mkdir -p "$dest_path"
            smart_copy "$src_path" "$dest_path" || return 1
        else
            if ! cmp -s "$src_path" "$dest_path" 2>/dev/null; then
                cp -d --remove-destination "$src_path" "$dest_path" || return 1
            fi
        fi
    done
}

refresh_system_caches() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Refresh system desktop and icon databases"
        return 0
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
    fi
    if [ -z "${XDG_CURRENT_DESKTOP:-}" ] || [[ "$XDG_CURRENT_DESKTOP" != *"KDE"* ]]; then
        command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
        command -v gtk4-update-icon-cache >/dev/null 2>&1 && gtk4-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
    fi
}
