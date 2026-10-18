#!/usr/bin/env bash
# scripts/legacy_migration.sh
# Legacy ACCELA path variables and migration helper functions

set_legacy_paths() {
    local base="${1:-$HOME}"
    LEGACY_ACCELA_DIR="$base/.local/share/ACCELA"
    LEGACY_APP_DIR="$LEGACY_ACCELA_DIR"
    LEGACY_MANIFESTS_DIR="$base/.local/share/ACCELA/morrenus_manifests"
    LEGACY_DEPOTS_DIR="$base/.local/share/ACCELA/depots"
    LEGACY_DESKTOP_NAME="accela.desktop"
    LEGACY_ICON_NAME="accela.png"
    LEGACY_HELPER_BIN="$base/.local/bin/accela-download"
    LEGACY_CACHE_DIR="$base/.cache/ACCELA"
    LEGACY_VENV_PYTHON="$base/.local/share/ACCELA/.venv/bin/python"
    LEGACY_BIN_NAME="accela"
    LEGACY_APP_BIN="$LEGACY_ACCELA_DIR/accela"
}

# Initialize variables for current $HOME
set_legacy_paths "${HOME:-}"

migrate_legacy_manifests() {
    local target_dest="${1:-}"
    if [ -n "$target_dest" ] && [ -d "$LEGACY_MANIFESTS_DIR" ]; then
        mkdir -p "$target_dest"
        cp -rn "$LEGACY_MANIFESTS_DIR"/* "$target_dest/" 2>/dev/null || true
    fi
    if [ -d "$LEGACY_MANIFESTS_DIR" ]; then
        chmod -R u+rwX "$LEGACY_MANIFESTS_DIR" 2>/dev/null || true
    fi
}

clean_legacy_binaries() {
    if [ -n "$LEGACY_APP_DIR" ] && [ -d "$LEGACY_APP_DIR" ]; then
        if [ -n "$LEGACY_MANIFESTS_DIR" ] && [ -d "$LEGACY_MANIFESTS_DIR" ]; then
            # Clean legacy binaries/files while strictly preserving morrenus_manifests intact
            find "$LEGACY_APP_DIR" -mindepth 1 -maxdepth 1 ! -name "$(basename "$LEGACY_MANIFESTS_DIR")" -exec rm -rf {} + 2>/dev/null || true
        else
            rm -rf "$LEGACY_APP_DIR"
        fi
    fi
}

clean_legacy_desktop_and_icons() {
    local base="${1:-$HOME}"
    local app_dir="$base/.local/share/applications"
    if [ -d "$app_dir" ] && [ -f "$app_dir/$LEGACY_DESKTOP_NAME" ]; then
        rm -f "$app_dir/$LEGACY_DESKTOP_NAME"
    fi

    if [ -n "$LEGACY_HELPER_BIN" ] && [ -e "$LEGACY_HELPER_BIN" ]; then
        rm -f "$LEGACY_HELPER_BIN"
    fi

    rm -f "$base/.local/share/icons/hicolor/"*/apps/"$LEGACY_ICON_NAME" 2>/dev/null || true
    rm -f "$base/.local/share/pixmaps/$LEGACY_ICON_NAME" 2>/dev/null || true
}

clean_legacy_cache() {
    if [ -n "$LEGACY_CACHE_DIR" ] && [ -d "$LEGACY_CACHE_DIR" ]; then
        rm -rf "$LEGACY_CACHE_DIR"
    fi
}

clean_legacy_all() {
    clean_legacy_binaries
    clean_legacy_desktop_and_icons "${HOME:-}"
    clean_legacy_cache
}
