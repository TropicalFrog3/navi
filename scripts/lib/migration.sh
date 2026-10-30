#!/usr/bin/env bash
# scripts/lib/migration.sh - Legacy ACCELA & SLSsteam migration and cleanup
set -u

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

    # Legacy SLSsteam paths
    LEGACY_SLS_APP_DIR="$base/.local/share/SLSsteam"
    LEGACY_SLS_FLATPAK_DIR="$base/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam"
    LEGACY_SLS_CONFIG_DIR="$base/.config/SLSsteam"
    LEGACY_SLS_CONFIG_BACKUPS="$base/.config/SLSsteam_backups"
    LEGACY_SLS_CACHE_DIR="$base/.cache/SLSsteam"
    LEGACY_SLS_STEAM_WRAPPER_BAK="$base/.local/bin/steam.pre-slssteam.bak"
    LEGACY_SLS_WRAPPER_PATTERN="SLSsteam"
    LEGACY_SLS_TRACKING_FILES=(
        "$base/.SLSsteam.log"
        "$base/.SLSsteam.json"
        "$base/.SLSsteam.installed"
        "$base/.SLSsteam.onlinefix"
        "$base/.SLSsteam.autocrack"
        "$base/.config/fish/SLSsteam.fish"
        "$base/.config/fish/conf.d/SLSsteam.fish"
        "/tmp/SLSsteam.API"
    )
}

clean_legacy_sls_binaries() {
    [ -n "${LEGACY_SLS_APP_DIR:-}" ] && [ -d "$LEGACY_SLS_APP_DIR" ] && rm -rf "$LEGACY_SLS_APP_DIR"
    [ -n "${LEGACY_SLS_FLATPAK_DIR:-}" ] && [ -d "$LEGACY_SLS_FLATPAK_DIR" ] && rm -rf "$LEGACY_SLS_FLATPAK_DIR"
}

clean_legacy_sls_tracking() {
    for f in "${LEGACY_SLS_TRACKING_FILES[@]:-}"; do
        [ -e "$f" ] && rm -f "$f"
    done
}

clean_legacy_sls_configs() {
    if [ -n "${LEGACY_SLS_CONFIG_DIR:-}" ] && [ -d "$LEGACY_SLS_CONFIG_DIR" ]; then
        for b in "$LEGACY_SLS_CONFIG_DIR"/updates.yaml.bak-*; do
            if [ -f "$b" ]; then
                mkdir -p "${LEGACY_SLS_CONFIG_BACKUPS:-$HOME/.config/SLSsteam_backups}"
                cp -a "$b" "${LEGACY_SLS_CONFIG_BACKUPS:-$HOME/.config/SLSsteam_backups}/"
            fi
        done

        local sls_cfg="$LEGACY_SLS_CONFIG_DIR/config.yaml"
        local cr_active=0
        if [ -f "$sls_cfg" ]; then
            if [ -f "$HOME/.config/navi/cloud_enabled" ] || \
               [ -d "$HOME/.config/CloudRedirect" ] || \
               [ -f "$HOME/.local/share/navi/cloudredirect/cloud_redirect.so" ] || \
               grep -qE 'NAVI CLOUD SHIM|DisableCloud|AdditionalApps' "$sls_cfg" 2>/dev/null; then
                cr_active=1
            fi
        fi

        if [ "$cr_active" -eq 1 ]; then
            find "$LEGACY_SLS_CONFIG_DIR" -mindepth 1 -maxdepth 1 ! -name "config.yaml" -exec rm -rf {} + 2>/dev/null || true
            type log_info >/dev/null 2>&1 && log_info "Preserved $sls_cfg for CloudRedirect."
        else
            rm -rf "$LEGACY_SLS_CONFIG_DIR"
        fi
    fi
    if [ -n "${LEGACY_SLS_CACHE_DIR:-}" ] && [ -d "$LEGACY_SLS_CACHE_DIR" ]; then
        rm -rf "$LEGACY_SLS_CACHE_DIR"
    fi
    return 0
}

clean_legacy_sls_integration() {
    local base="${1:-$HOME}"
    # Wrapper backups
    if [ -f "$base/.local/bin/steam.pre-slssteam.bak" ] && [ ! -f "$base/.local/bin/steam.pre-navi.bak" ]; then
        mv -f "$base/.local/bin/steam.pre-slssteam.bak" "$base/.local/bin/steam.pre-navi.bak"
    elif [ -f "$base/.local/bin/steam.pre-slssteam.bak" ]; then
        rm -f "$base/.local/bin/steam.pre-slssteam.bak"
    fi

    # Shell configuration cleanup
    for profile in "$base/.profile" "$base/.bashrc"; do
        if [ -f "$profile" ] && grep -qi 'SLSsteam' "$profile" 2>/dev/null; then
            sed -i '/[Ss][Ll][Ss][Ss][Tt][Ee][Aa][Mm]/d' "$profile" 2>/dev/null || true
        fi
    done

    # Desktop entries
    local app_dir="$base/.local/share/applications"
    if [ -d "$app_dir" ]; then
        for entry in "steam.desktop" "steam-native.desktop"; do
            if [ -f "$app_dir/$entry" ] && grep -qi 'SLSsteam' "$app_dir/$entry" 2>/dev/null; then
                rm -f "$app_dir/$entry"
            fi
        done
    fi

    # Native Steam launcher (steam.sh) restoration and cleanup
    local steam_roots=(
        "$base/.local/share/Steam"
        "$base/.steam/steam"
        "$base/.steam/root"
        "$base/.var/app/com.valvesoftware.Steam/.local/share/Steam"
    )
    local visited_roots=""

    for sdir in "${steam_roots[@]}"; do
        [ -d "$sdir" ] || continue
        local canonical_dir
        canonical_dir="$(readlink -f "$sdir" 2>/dev/null || echo "$sdir")"
        case ":$visited_roots:" in
            *:"$canonical_dir":*) continue ;;
        esac
        visited_roots="$visited_roots:$canonical_dir"

        local launcher="$canonical_dir/steam.sh"
        [ -f "$launcher" ] || continue

        if grep -qiE 'SLSsteam|INJECT_SLS|headcrab' "$launcher" 2>/dev/null; then
            local client_sh="$canonical_dir/client.sh"
            local bak_cand=""

            if [ -f "$canonical_dir/steam.sh.bak" ] && ! grep -qiE 'SLSsteam|INJECT_SLS' "$canonical_dir/steam.sh.bak" 2>/dev/null; then
                bak_cand="$canonical_dir/steam.sh.bak"
            elif [ -f "$canonical_dir/steam.sh.headcrab-wrapper.bak" ] && ! grep -qiE 'SLSsteam|INJECT_SLS' "$canonical_dir/steam.sh.headcrab-wrapper.bak" 2>/dev/null; then
                bak_cand="$canonical_dir/steam.sh.headcrab-wrapper.bak"
            fi

            if [ -f "$client_sh" ] && ! grep -qiE 'SLSsteam|INJECT_SLS' "$client_sh" 2>/dev/null; then
                if [ "${DRY_RUN:-0}" -eq 1 ]; then
                    type log_dry >/dev/null 2>&1 && log_dry "Restore $launcher from $client_sh and remove $client_sh"
                else
                    chmod u+w "$launcher" 2>/dev/null || true
                    rm -f "$launcher" 2>/dev/null || true
                    mv -f "$client_sh" "$launcher"
                    chmod 755 "$launcher" 2>/dev/null || chmod +x "$launcher" 2>/dev/null || true
                    type log_info >/dev/null 2>&1 && log_info "Restored native steam.sh from $client_sh"
                fi
            elif [ -n "$bak_cand" ]; then
                if [ "${DRY_RUN:-0}" -eq 1 ]; then
                    type log_dry >/dev/null 2>&1 && log_dry "Restore $launcher from $bak_cand and remove $bak_cand"
                else
                    chmod u+w "$launcher" 2>/dev/null || true
                    rm -f "$launcher" 2>/dev/null || true
                    mv -f "$bak_cand" "$launcher"
                    chmod 755 "$launcher" 2>/dev/null || chmod +x "$launcher" 2>/dev/null || true
                    type log_info >/dev/null 2>&1 && log_info "Restored native steam.sh from $bak_cand"
                fi
            else
                if [ "${DRY_RUN:-0}" -eq 1 ]; then
                    type log_dry >/dev/null 2>&1 && log_dry "Strip legacy SLSsteam and INJECT_SLS references from $launcher in-place"
                else
                    chmod u+w "$launcher" 2>/dev/null || true
                    sed -i -E '/[Ss][Ll][Ss][Ss][Tt][Ee][Aa][Mm]|[Ii][Nn][Jj][Ee][Cc][Tt]_[Ss][Ll][Ss]/d' "$launcher" 2>/dev/null || true
                    chmod 755 "$launcher" 2>/dev/null || chmod +x "$launcher" 2>/dev/null || true
                    type log_info >/dev/null 2>&1 && log_info "Stripped legacy SLS references from $launcher"
                fi
            fi
        fi

        # Clean up any lingering contaminated backup files in the steam root
        for bak_file in "$canonical_dir"/steam.sh.bak "$canonical_dir"/steam.sh.headcrab-wrapper.bak "$canonical_dir"/steam.sh.headcrab-*; do
            [ -f "$bak_file" ] || continue
            if grep -qiE 'SLSsteam|INJECT_SLS|headcrab' "$bak_file" 2>/dev/null; then
                if [ "${DRY_RUN:-0}" -eq 1 ]; then
                    type log_dry >/dev/null 2>&1 && log_dry "Remove contaminated backup: $bak_file"
                else
                    rm -f "$bak_file" 2>/dev/null || true
                fi
            fi
        done
    done
}

migrate_legacy_manifests() {
    local target_dest="${1:-$HOME/.local/share/navi/manifests}"
    if [ -d "${LEGACY_MANIFESTS_DIR:-}" ]; then
        mkdir -p "$target_dest"
        cp -rn "$LEGACY_MANIFESTS_DIR"/* "$target_dest/" 2>/dev/null || true
        chmod -R u+rwX "$LEGACY_MANIFESTS_DIR" 2>/dev/null || true
    fi
}

migrate_legacy_configs() {
    local base="${1:-$HOME}"
    local legacy_cfg="$base/.config/SLSsteam"
    local navi_cfg="$base/.config/navi"

    if [ -d "$legacy_cfg" ]; then
        mkdir -p "$navi_cfg"
        for item in "$legacy_cfg"/*; do
            [ -e "$item" ] || continue
            local bname="$(basename "$item")"
            if [ ! -e "$navi_cfg/$bname" ]; then
                cp -a "$item" "$navi_cfg/$bname"
            fi
        done
    fi
}

clean_legacy_binaries() {
    if [ -n "${LEGACY_APP_DIR:-}" ] && [ -d "$LEGACY_APP_DIR" ]; then
        if [ -n "${LEGACY_MANIFESTS_DIR:-}" ] && [ -d "$LEGACY_MANIFESTS_DIR" ]; then
            find "$LEGACY_APP_DIR" -mindepth 1 -maxdepth 1 ! -name "$(basename "$LEGACY_MANIFESTS_DIR")" -exec rm -rf {} + 2>/dev/null || true
        else
            rm -rf "$LEGACY_APP_DIR"
        fi
    fi
}

clean_legacy_desktop_and_icons() {
    local base="${1:-$HOME}"
    local app_dir="$base/.local/share/applications"
    if [ -d "$app_dir" ] && [ -f "$app_dir/${LEGACY_DESKTOP_NAME:-}" ]; then
        rm -f "$app_dir/$LEGACY_DESKTOP_NAME"
    fi

    if [ -n "${LEGACY_HELPER_BIN:-}" ] && [ -e "$LEGACY_HELPER_BIN" ]; then
        rm -f "$LEGACY_HELPER_BIN"
    fi

    if [ -n "${LEGACY_ICON_NAME:-}" ]; then
        rm -f "$base/.local/share/icons/hicolor/"*/apps/"$LEGACY_ICON_NAME" 2>/dev/null || true
        rm -f "$base/.local/share/pixmaps/$LEGACY_ICON_NAME" 2>/dev/null || true
    fi
}

clean_legacy_cache() {
    if [ -n "${LEGACY_CACHE_DIR:-}" ] && [ -d "$LEGACY_CACHE_DIR" ]; then
        rm -rf "$LEGACY_CACHE_DIR"
    fi
}

clean_legacy_all() {
    clean_legacy_binaries
    clean_legacy_desktop_and_icons "${HOME:-}"
    clean_legacy_cache
}

# High-level entrypoints called by dispatcher / other modules
migration_run_install() {
    set_legacy_paths "${HOME:-}"

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Migrate legacy manifests: $LEGACY_MANIFESTS_DIR -> \$HOME/.local/share/navi/manifests (preserving original)"
        log_dry "Migrate legacy configs: $LEGACY_SLS_CONFIG_DIR -> \$HOME/.config/navi (copy-first, preserving original)"
        log_dry "Clean legacy ACCELA binaries, desktop entries, icons, and cache"
        log_dry "Clean legacy SLSsteam binaries, LD_AUDIT references, and tracking files"
        return 0
    fi

    migrate_legacy_manifests "$HOME/.local/share/navi/manifests"
    migrate_legacy_configs "$HOME"
    clean_legacy_binaries
    clean_legacy_desktop_and_icons "$HOME"
    clean_legacy_cache
    clean_legacy_sls_binaries
    clean_legacy_sls_tracking
    clean_legacy_sls_integration "$HOME"
}

migration_run_update() {
    migration_run_install
}

migration_run_uninstall() {
    local remove_configs="${1:-0}"
    set_legacy_paths "${HOME:-}"

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Clean legacy ACCELA binaries, desktop entries, icons, and cache (preserving morrenus_manifests)"
        log_dry "Clean legacy SLSsteam binaries, integration wrappers, and tracking files"
        if [ "$remove_configs" = "1" ]; then
            log_dry "Clean legacy SLSsteam configs (preserving updates.yaml backups and config.yaml if active for CloudRedirect)"
        fi
        return 0
    fi

    clean_legacy_binaries
    clean_legacy_desktop_and_icons "$HOME"
    clean_legacy_cache
    clean_legacy_sls_binaries
    clean_legacy_sls_tracking
    clean_legacy_sls_integration "$HOME"

    if [ "$remove_configs" = "1" ]; then
        clean_legacy_sls_configs
    fi
}

# Auto-initialize paths for current $HOME
set_legacy_paths "${HOME:-}"
