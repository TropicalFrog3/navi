#!/usr/bin/env bash
# @file  update.sh
# @brief Updater script pulling new releases and refreshing binaries
# scripts/lib/update.sh - Update module for Navi payload and integration
set -u

do_update() {
    log_info "Starting Navi update..."

    # 1. Steam running check
    check_steam_running || exit 1

    # 2. Run legacy migration
    if type migration_run_update >/dev/null 2>&1; then
        migration_run_update
    fi

    # 3. Dry-run preview
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Update Navi payload in $HOME/.local/share/navi (strictly preserving manifests)"
        if [ -d "$HOME/.var/app/com.valvesoftware.Steam" ]; then
            log_dry "Update Navi Flatpak payload and environment overrides"
        fi
        log_dry "Update safe Steam wrapper and desktop integration"
        log_dry "Merge updates.yaml and refresh configuration in $HOME/.config/navi"
        log_dry "Refresh system desktop and icon databases"
        log_dry "Navi update preview complete (0 files modified)"
        return 0
    fi

    # 4. Source install helpers if not already defined
    if ! type find_payload_bin_dir >/dev/null 2>&1; then
        # shellcheck source=scripts/lib/install.sh
        source "$REPO_ROOT/scripts/lib/install.sh"
    fi

    local bin_dir
    local res_dir
    bin_dir="$(find_payload_bin_dir || true)"
    res_dir="$(find_payload_res_dir || true)"

    if [ -z "$bin_dir" ] || [ ! -f "$bin_dir/libnavi.so" ]; then
        log_error "libnavi.so not found! Please compile or place binaries in bin/ before updating."
        exit 1
    fi

    local navi_share="$HOME/.local/share/navi"
    mkdir -p "$navi_share"
    mkdir -p "$navi_share/path"

    # Copy updated binaries (preserving manifests directory intact)
    cp -f "$bin_dir/libnavi.so" "$navi_share/libnavi.so"
    [ -f "$bin_dir/library-inject.so" ] && cp -f "$bin_dir/library-inject.so" "$navi_share/library-inject.so"

    if [ -n "$res_dir" ] && [ -d "$res_dir" ]; then
        mkdir -p "$navi_share/res"
        smart_copy "$res_dir" "$navi_share/res"
    fi

    # Update Flatpak Steam if present
    local flatpak_steam_dir="$HOME/.var/app/com.valvesoftware.Steam"
    if [ -d "$flatpak_steam_dir" ] || (command -v flatpak >/dev/null 2>&1 && flatpak info com.valvesoftware.Steam >/dev/null 2>&1); then
        local flatpak_navi="$flatpak_steam_dir/.local/share/navi"
        mkdir -p "$flatpak_navi"
        cp -f "$bin_dir/libnavi.so" "$flatpak_navi/"
        [ -f "$bin_dir/library-inject.so" ] && cp -f "$bin_dir/library-inject.so" "$flatpak_navi/"
        if [ -n "$res_dir" ] && [ -d "$res_dir" ]; then
            mkdir -p "$flatpak_navi/res"
            smart_copy "$res_dir" "$flatpak_navi/res"
        fi
        if command -v flatpak >/dev/null 2>&1; then
            local fp_audit="/app/links/\$LIB/libshared-library-guard.so:$flatpak_navi/library-inject.so:$flatpak_navi/libnavi.so"
            flatpak override --user --env=LD_AUDIT="$fp_audit" --env=SHARED_LIBRARY_GUARD=0 com.valvesoftware.Steam 2>/dev/null || true
        fi
    fi

    # Update wrappers & desktop files
    install_safe_wrapper
    install_desktop_entries
    install_steamos_systemd_integration

    # Merge configuration & updates.yaml
    if [ -n "$res_dir" ]; then
        merge_updates_configuration "$res_dir"
    fi

    # Refresh caches
    refresh_system_caches

    log_info "Navi update completed successfully!"
}
