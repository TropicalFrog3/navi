#!/usr/bin/env bash
# scripts/lib/uninstall.sh - Complete uninstaller module for Navi
set -u

confirm_uninstall() {
    if [ "${ASSUME_YES:-0}" = "1" ] || [ "${SLS_TEST_ENV:-0}" = "1" ] || [ "${SLS_UNINSTALL_AUTO_ACCEPT:-0}" = "1" ] || [ ! -t 0 ]; then
        return 0
    fi
    if has_zenity; then
        zenity --question \
            --title="Navi Uninstaller" \
            --text="Are you sure you want to completely uninstall Navi and associated components?" \
            --ok-label="Uninstall" --cancel-label="Cancel" \
            --width=450 2>/dev/null
        return $?
    else
        echo -e "${YELLOW}====================================================${NC}"
        echo -e "${YELLOW}           Navi Uninstallation                      ${NC}"
        echo -e "${YELLOW}====================================================${NC}"
        read -r -p "Are you sure you want to completely uninstall Navi? [y/N]: " response
        case "$response" in
            [yY][eE][sS]|[yY])
                return 0
                ;;
            *)
                return 1
                ;;
        esac
    fi
}

ask_remove_configs() {
    if [ "${SLS_UNINSTALL_REMOVE_CONFIGS:-0}" = "1" ]; then
        return 0 # Delete configs
    fi
    if [ "${SLS_UNINSTALL_KEEP_CONFIGS:-0}" = "1" ] || [ "${ASSUME_YES:-0}" = "1" ] || [ "${SLS_TEST_ENV:-0}" = "1" ] || [ ! -t 0 ]; then
        return 1 # Keep configs
    fi
    if has_zenity; then
        if zenity --question \
            --title="Remove User Configurations?" \
            --text="Do you also want to remove your user configuration and saved data?\n\n- ~/.config/navi\n- ~/.config/Tachibana Labs\n\n(Click 'No' to preserve your settings)" \
            --ok-label="Keep Configs" --cancel-label="Delete Configs" \
            --width=500 2>/dev/null; then
            return 1 # Keep configs
        else
            return 0 # Delete configs
        fi
    else
        echo ""
        read -r -p "Do you want to delete your user configurations (~/.config/navi, ~/.config/Tachibana Labs)? [y/N]: " response
        case "$response" in
            [yY][eE][sS]|[yY])
                return 0
                ;;
            *)
                return 1
                ;;
        esac
    fi
}

cleanup_slssteam_cloud_shim() {
    local sls_cfg="$HOME/.config/SLSsteam/config.yaml"
    if [ ! -f "$sls_cfg" ]; then
        return 0
    fi

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Clean Navi cloud shim from $sls_cfg (preserving any remaining user configuration)"
        return 0
    fi

    if command -v python3 >/dev/null 2>&1; then
        python3 - <<'EOF' "$sls_cfg"
import sys, os

cfg_path = sys.argv[1]
try:
    with open(cfg_path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()
except Exception:
    sys.exit(0)

new_lines = []
in_shim = False
had_shim = False

for line in lines:
    if "# >>> NAVI CLOUD SHIM" in line:
        in_shim = True
        had_shim = True
        continue
    if in_shim:
        if "# <<< NAVI CLOUD SHIM" in line:
            in_shim = False
        continue
    new_lines.append(line)

# Determine if there is remaining user content
remaining_content = [l for l in new_lines if l.strip()]

if not remaining_content:
    # File only contained the shim or was empty -> remove it
    try:
        os.remove(cfg_path)
    except OSError:
        pass
    sls_dir = os.path.dirname(cfg_path)
    try:
        if os.path.isdir(sls_dir) and not os.listdir(sls_dir):
            os.rmdir(sls_dir)
    except OSError:
        pass
else:
    # Preserve remaining user content with shim removed
    try:
        with open(cfg_path, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
    except Exception:
        pass
EOF
    else
        local tmp
        tmp="$(mktemp "${TMPDIR:-/tmp}/slscfg.XXXXXX")"
        sed '/# >>> NAVI CLOUD SHIM/,/# <<< NAVI CLOUD SHIM/d' "$sls_cfg" > "$tmp" 2>/dev/null || cp "$sls_cfg" "$tmp"
        if grep -qE '[^[:space:]]' "$tmp" 2>/dev/null; then
            mv -f "$tmp" "$sls_cfg"
        else
            rm -f "$tmp" "$sls_cfg"
            rmdir "$HOME/.config/SLSsteam" 2>/dev/null || true
        fi
    fi
}

do_uninstall() {
    # 1. Steam running check
    check_steam_running || exit 1

    local remove_configs=0
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Would confirm uninstall"
        remove_configs=0
    else
        # 2. Confirmation
        if ! confirm_uninstall; then
            log_info "Uninstallation cancelled by user."
            exit 0
        fi

        # 3. Ask config removal
        if ask_remove_configs; then
            remove_configs=1
        fi
    fi

    # 4. Dry-run preview
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Remove Navi application files from $HOME/.local/share/navi (strictly preserving manifests)"
        log_dry "Remove Navi CloudRedirect payload and idle helper (preserving ~/.config/CloudRedirect/ and save data)"
        log_dry "Restore native Steam launchers and remove wrappers in $HOME/.local/bin"
        log_dry "Remove Navi desktop entries and icons"
        log_dry "Reset Flatpak Steam environment overrides"
        log_dry "Clean tracking and log files"
        if [ -f "$HOME/.config/SLSsteam/config.yaml" ]; then
            cleanup_slssteam_cloud_shim
        fi
        if [ "$remove_configs" -eq 1 ]; then
            log_dry "Remove user configuration directories (preserving updates.yaml backups)"
        else
            log_dry "Preserve user configuration directories intact"
        fi
        if type migration_run_uninstall >/dev/null 2>&1; then
            migration_run_uninstall "$remove_configs"
        fi
        refresh_system_caches
        log_dry "Navi uninstallation preview complete (0 files modified)"
        return 0
    fi

    log_info "Starting uninstallation process..."

    # Category 1: Application Data & Binaries
    # Preserving manifests directory in ~/.local/share/navi
    local navi_dir="$HOME/.local/share/navi"
    if [ -d "$navi_dir" ]; then
        if [ -d "$navi_dir/manifests" ]; then
            find "$navi_dir" -mindepth 1 -maxdepth 1 ! -name "manifests" -exec rm -rf {} + 2>/dev/null || true
            log_info "Cleaned Navi binaries while strictly preserving manifests directory intact."
        else
            rm -rf "$navi_dir"
            log_info "Removed directory: $navi_dir"
        fi
    fi

    # Remove Navi-owned CloudRedirect payload & idle helper
    # HARD INVARIANT: NEVER delete or touch $HOME/.config/CloudRedirect/ or user save directories (storage/, userdata/).
    rm -rf "$HOME/.local/share/navi/cloudredirect"
    rm -f "$HOME/.local/share/navi/bin/navi-cloud-apply-pending"
    rm -f "$HOME/.config/navi/cloud_enabled"

    local flatpak_navi="$HOME/.var/app/com.valvesoftware.Steam/.local/share/navi"
    if [ -d "$flatpak_navi" ]; then
        rm -rf "$flatpak_navi/cloudredirect"
        rm -f "$flatpak_navi/bin/navi-cloud-apply-pending"
        rm -f "$HOME/.var/app/com.valvesoftware.Steam/.config/navi/cloud_enabled"
        rm -rf "$flatpak_navi"
        log_info "Removed Flatpak Navi directory: $flatpak_navi"
    fi

    if [ -d "$HOME/.headcrab" ]; then
        rm -rf "$HOME/.headcrab"
        log_info "Removed directory: $HOME/.headcrab"
    fi

    # Non-destructive SLSsteam configuration handling
    cleanup_slssteam_cloud_shim

    # Run legacy migration cleanups
    if type migration_run_uninstall >/dev/null 2>&1; then
        migration_run_uninstall "$remove_configs"
    fi

    # Category 2: System & Steam Integration
    local steam_wrapper="$HOME/.local/bin/steam"
    local steam_wrapper_bak="$HOME/.local/bin/steam.pre-navi.bak"

    if [ -f "$steam_wrapper_bak" ]; then
        mv -f "$steam_wrapper_bak" "$steam_wrapper"
        chmod 755 "$steam_wrapper"
        log_info "Restored original Steam wrapper from $steam_wrapper_bak"
    elif [ -f "$steam_wrapper" ]; then
        if grep -qE 'navi/library-inject|LD_AUDIT' "$steam_wrapper" 2>/dev/null; then
            rm -f "$steam_wrapper"
            log_info "Removed Navi Steam wrapper: $steam_wrapper"
        fi
    fi

    # Native steam.sh restoration if patched by headcrab
    for steam_root in "$HOME/.local/share/Steam" "$HOME/.steam/steam"; do
        local launcher="$steam_root/steam.sh"
        local bak="$steam_root/steam.sh.bak"
        local headcrab_bak="$steam_root/steam.sh.headcrab-wrapper.bak"

        if [ -f "$headcrab_bak" ]; then
            if ! grep -qE 'LD_AUDIT' "$headcrab_bak" 2>/dev/null; then
                mv -f "$headcrab_bak" "$launcher"
                chmod 755 "$launcher"
                log_info "Restored native steam.sh from $headcrab_bak"
            else
                rm -f "$headcrab_bak"
            fi
        elif [ -f "$bak" ] && [ -f "$launcher" ]; then
            if grep -qE 'LD_AUDIT' "$launcher" 2>/dev/null && ! grep -qE 'LD_AUDIT' "$bak" 2>/dev/null; then
                mv -f "$bak" "$launcher"
                chmod 755 "$launcher"
                log_info "Restored native steam.sh from $bak"
            fi
        fi
    done

    # Desktop entries & Icons
    local desktop_dir="$HOME/.local/share/applications"
    if [ -d "$desktop_dir" ]; then
        for entry in "headcrab.desktop" "navi.desktop" "navi-native.desktop"; do
            if [ -f "$desktop_dir/$entry" ]; then
                rm -f "$desktop_dir/$entry"
                log_info "Removed desktop entry: $desktop_dir/$entry"
            fi
        done

        for steam_desk in "steam.desktop" "steam-native.desktop"; do
            local target="$desktop_dir/$steam_desk"
            if [ -f "$target" ] && grep -qE 'navi/library-inject|LD_AUDIT' "$target" 2>/dev/null; then
                rm -f "$target"
                log_info "Removed modified desktop entry: $target"
            fi
        done
    fi

    # Autostart integration cleanup
    local autostart_steam="$HOME/.config/autostart/steam.desktop"
    if [ -f "$autostart_steam" ] && grep -qE 'navi/library-inject|LD_AUDIT' "$autostart_steam" 2>/dev/null; then
        rm -f "$autostart_steam"
        log_info "Removed autostart launcher: $autostart_steam"
    fi

    # User Desktop shortcut cleanup
    local user_desktop="$HOME/Desktop/steam.desktop"
    if [ -L "$user_desktop" ]; then
        local target_link
        target_link="$(readlink "$user_desktop")"
        if [[ "$target_link" == *"navi"* ]] || [[ "$target_link" == *".local/share/applications/steam.desktop"* ]]; then
            if [ -f "/usr/share/applications/steam.desktop" ]; then
                ln -sf "/usr/share/applications/steam.desktop" "$user_desktop"
                log_info "Restored native Desktop steam.desktop symlink."
            fi
        fi
    elif [ -f "$user_desktop" ] && grep -qE 'navi/library-inject|LD_AUDIT' "$user_desktop" 2>/dev/null; then
        sed -i 's|^Exec=env LD_AUDIT="[^"]*" |Exec=|' "$user_desktop" 2>/dev/null || true
    fi

    # SteamOS systemd drop-in cleanup
    local dropin_file="$HOME/.config/systemd/user/steam-launcher.service.d/navi.conf"
    if [ -f "$dropin_file" ]; then
        rm -f "$dropin_file"
        rmdir "$HOME/.config/systemd/user/steam-launcher.service.d" 2>/dev/null || true
        if command -v systemctl >/dev/null 2>&1; then
            systemctl --user daemon-reload 2>/dev/null || true
        fi
        log_info "Removed SteamOS Gaming Mode drop-in: $dropin_file"
    fi

    # Systemd environment.d cleanup
    local env_file="$HOME/.config/environment.d/10-navi-path.conf"
    if [ -f "$env_file" ]; then
        rm -f "$env_file"
        log_info "Removed systemd environment file: $env_file"
    fi

    rm -f "$HOME/.local/share/icons/hicolor/"*/apps/headcrab.png 2>/dev/null || true
    rm -f "$HOME/.local/share/icons/hicolor/"*/apps/navi.png 2>/dev/null || true

    # Flatpak environment overrides
    if command -v flatpak >/dev/null 2>&1; then
        flatpak override --user --unset-env=LD_AUDIT com.valvesoftware.Steam 2>/dev/null || true
        flatpak override --user --unset-env=SHARED_LIBRARY_GUARD com.valvesoftware.Steam 2>/dev/null || true
        log_info "Reset Flatpak Steam environment overrides."
    fi

    # Tracking & log files
    for track in \
        "$HOME/.navi.log" \
        "$HOME/.navi.json" \
        "$HOME/.navi.installed" \
        "$HOME/.navi.onlinefix" \
        "$HOME/.navi.autocrack" \
        "$HOME/.config/fish/navi.fish" \
        "$HOME/.config/fish/conf.d/navi.fish" \
        "/tmp/navi.API"; do
        if [ -e "$track" ]; then
            rm -f "$track"
            log_info "Removed tracking/log file: $track"
        fi
    done

    # Category 3: User Configurations (Conditional)
    if [ "$remove_configs" -eq 1 ]; then
        log_info "Removing user configuration and cache files..."
        if [ -d "$HOME/.config/navi" ]; then
            for b in "$HOME/.config/navi"/updates.yaml.bak-*; do
                if [ -f "$b" ]; then
                    mkdir -p "$HOME/.config/navi_backups"
                    cp -a "$b" "$HOME/.config/navi_backups/"
                    log_info "Preserved updates.yaml backup: $(basename "$b") in ~/.config/navi_backups/"
                fi
            done
        fi

        for cdir in \
            "$HOME/.config/navi" \
            "$HOME/.config/Tachibana Labs" \
            "$HOME/.local/share/navi/cache" \
            "$HOME/.local/share/navi/cache_backups" \
            "$HOME/.cache/navi"; do
            if [ -d "$cdir" ]; then
                rm -rf "$cdir"
                log_info "Removed configuration/cache directory: $cdir"
            fi
        done
    else
        log_info "Preserving user configurations at ~/.config/navi and ~/.config/Tachibana Labs."
    fi

    refresh_system_caches

    log_info "Uninstallation completed successfully!"
    if has_zenity && [ "${SLS_UNINSTALL_AUTO_ACCEPT:-0}" != "1" ]; then
        zenity --info \
            --title="Navi Uninstaller" \
            --text="Uninstallation completed successfully!\n\nAll components have been cleanly removed." \
            --width=350 2>/dev/null || true
    fi
}
