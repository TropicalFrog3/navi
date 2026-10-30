#!/usr/bin/env bash
# scripts/lib/install.sh - Navi payload and environment installer
set -u

find_payload_bin_dir() {
    local cand
    for cand in \
        "$REPO_ROOT/bin" \
        "$REPO_ROOT/navi/bin" \
        "$REPO_ROOT/navi" \
        "$REPO_ROOT"; do
        if [ -f "$cand/libnavi.so" ]; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

find_payload_res_dir() {
    local cand
    for cand in \
        "$REPO_ROOT/res" \
        "$REPO_ROOT/navi/res"; do
        if [ -d "$cand" ]; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

install_safe_wrapper() {
    local wrapper_dir="$HOME/.local/bin"
    local wrapper="$wrapper_dir/steam"
    local native_steam
    native_steam="$(find_native_steam)"

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Install safe Steam wrapper at $wrapper pointing to $native_steam"
        log_dry "Ensure $wrapper_dir is in PATH in ~/.profile and ~/.bashrc"
        return 0
    fi

    # Restore steam.sh backup if patched with outer wrapper
    for steam_root in "$HOME/.local/share/Steam" "$HOME/.steam/steam"; do
        local launcher="$steam_root/steam.sh"
        local bak="$steam_root/steam.sh.bak"
        if [ -f "$bak" ] && [ -f "$launcher" ] && grep -qE 'LD_AUDIT' "$launcher" 2>/dev/null; then
            if ! grep -qE 'LD_AUDIT' "$bak" 2>/dev/null; then
                cp -f "$bak" "$launcher"
                chmod 755 "$launcher"
                rm -f "$bak"
                log_info "Restored native steam.sh launcher from backup."
            fi
        fi
    done

    mkdir -p "$wrapper_dir"
    if [ -e "$wrapper" ] && ! grep -q 'navi/library-inject.so' "$wrapper" 2>/dev/null; then
        mv -f "$wrapper" "$wrapper.pre-navi.bak"
    fi

    cat > "$wrapper" <<EOF
#!/bin/sh
# Navi Steam wrapper with dynamic CloudRedirect preload and idle pending-ops execution
CR_PENDING="\$HOME/.local/share/navi/bin/navi-cloud-apply-pending"
if [ -x "\$CR_PENDING" ]; then
    "\$CR_PENDING" --process-all 2>/dev/null || true
fi

CR_SO="\$HOME/.local/share/navi/cloudredirect/cloud_redirect.so"
if [ -f "\$HOME/.config/navi/cloud_enabled" ] && [ ! -f "\$HOME/.config/CloudRedirect/disable" ] && [ -f "\$CR_SO" ]; then
    case ":\${LD_PRELOAD:-}:" in
        *":\$CR_SO:"*) ;;
        *) export LD_PRELOAD="\${CR_SO}\${LD_PRELOAD:+:\${LD_PRELOAD}}" ;;
    esac
fi

exec env LD_AUDIT="\$HOME/.local/share/navi/library-inject.so:\$HOME/.local/share/navi/libnavi.so" "$native_steam" "\$@"
EOF
    chmod 755 "$wrapper"

    # Make wrapper discoverable in fresh login/interactive shells
    local path_line='export PATH="$HOME/.local/bin:$PATH"'
    for profile in "$HOME/.profile" "$HOME/.bashrc"; do
        if [ -f "$profile" ]; then
            if ! grep -Fqx "$path_line" "$profile" 2>/dev/null; then
                printf '\n%s\n' "$path_line" >> "$profile"
            fi
        elif [ ! -e "$profile" ]; then
            echo "$path_line" > "$profile"
        fi
    done

    # Additional wrappers in ~/.local/share/navi/path
    local navi_path="$HOME/.local/share/navi/path"
    mkdir -p "$navi_path"
    for exe in steam steam-runtime steam-native; do
        local sys_bin
        sys_bin="$(type -P "$exe" 2>/dev/null || true)"
        if [ -n "$sys_bin" ] && [ -x "$sys_bin" ] && [ "$sys_bin" != "$navi_path/$exe" ]; then
            cat > "$navi_path/$exe" <<EOF
#!/bin/sh
# Navi Steam wrapper with dynamic CloudRedirect preload and idle pending-ops execution
CR_PENDING="\$HOME/.local/share/navi/bin/navi-cloud-apply-pending"
if [ -x "\$CR_PENDING" ]; then
    "\$CR_PENDING" --process-all 2>/dev/null || true
fi

CR_SO="\$HOME/.local/share/navi/cloudredirect/cloud_redirect.so"
if [ -f "\$HOME/.config/navi/cloud_enabled" ] && [ ! -f "\$HOME/.config/CloudRedirect/disable" ] && [ -f "\$CR_SO" ]; then
    case ":\${LD_PRELOAD:-}:" in
        *":\$CR_SO:"*) ;;
        *) export LD_PRELOAD="\${CR_SO}\${LD_PRELOAD:+:\${LD_PRELOAD}}" ;;
    esac
fi

exec env LD_AUDIT="\$HOME/.local/share/navi/library-inject.so:\$HOME/.local/share/navi/libnavi.so" "$sys_bin" "\$@"
EOF
            chmod 755 "$navi_path/$exe"
        fi
    done
}

install_desktop_entries() {
    local sys_apps="/usr/share/applications"
    local user_apps="$HOME/.local/share/applications"
    local autostart_dir="$HOME/.config/autostart"
    local desktop_dir="$HOME/Desktop"

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Install desktop integration files into $user_apps"
        log_dry "Configure desktop autostart and desktop launcher entries"
        return 0
    fi

    mkdir -p "$user_apps"
    for name in steam steam-native; do
        if [ -f "$sys_apps/$name.desktop" ]; then
            cp -f "$sys_apps/$name.desktop" "$user_apps/"
            sed -i "/^Exec=/ { s|^Exec=/|Exec=env LD_AUDIT=\"$HOME/.local/share/navi/library-inject.so:$HOME/.local/share/navi/libnavi.so\" /| }" "$user_apps/$name.desktop"
        fi
    done

    # Autostart integration (overrides /etc/xdg/autostart/steam.desktop when Desktop Mode starts)
    if [ -f "/etc/xdg/autostart/steam.desktop" ] || [ "${NAVI_FORCE_STEAMOS:-0}" = "1" ] || [ -f "$autostart_dir/steam.desktop" ]; then
        mkdir -p "$autostart_dir"
        if [ -f "/etc/xdg/autostart/steam.desktop" ]; then
            cp -f "/etc/xdg/autostart/steam.desktop" "$autostart_dir/steam.desktop"
            sed -i "/^Exec=/ { s|^Exec=/|Exec=env LD_AUDIT=\"$HOME/.local/share/navi/library-inject.so:$HOME/.local/share/navi/libnavi.so\" /| }" "$autostart_dir/steam.desktop"
        else
            cat > "$autostart_dir/steam.desktop" <<EOF
[Desktop Entry]
Name=Steam
Exec=env LD_AUDIT="$HOME/.local/share/navi/library-inject.so:$HOME/.local/share/navi/libnavi.so" /usr/bin/steam -silent %U
Icon=steam
Terminal=false
Type=Application
Categories=Network;FileTransfer;Game;
OnlyShowIn=KDE
EOF
        fi
        log_info "Configured desktop autostart launcher at $autostart_dir/steam.desktop"
    fi

    # User Desktop shortcut integration (~/Desktop/steam.desktop)
    if [ -e "$desktop_dir/steam.desktop" ] || [ -L "$desktop_dir/steam.desktop" ]; then
        if [ -L "$desktop_dir/steam.desktop" ]; then
            local current_link
            current_link="$(readlink "$desktop_dir/steam.desktop")"
            if [ "$current_link" != "$user_apps/steam.desktop" ]; then
                ln -sf "$user_apps/steam.desktop" "$desktop_dir/steam.desktop"
                log_info "Updated $desktop_dir/steam.desktop symlink to point to Navi wrapper."
            fi
        elif [ -f "$desktop_dir/steam.desktop" ]; then
            sed -i "/^Exec=/ { s|^Exec=/|Exec=env LD_AUDIT=\"$HOME/.local/share/navi/library-inject.so:$HOME/.local/share/navi/libnavi.so\" /| }" "$desktop_dir/steam.desktop"
            log_info "Patched $desktop_dir/steam.desktop with Navi LD_AUDIT wrapper."
        fi
    fi
}

install_steamos_systemd_integration() {
    local is_steamos=0
    if [ "${NAVI_FORCE_STEAMOS:-0}" = "1" ] || \
       [ -f /usr/lib/systemd/user/steam-launcher.service ] || \
       ( [ -f /etc/os-release ] && grep -qi "steamos" /etc/os-release 2>/dev/null ) || \
       command -v steamosctl >/dev/null 2>&1; then
        is_steamos=1
    fi

    local dropin_dir="$HOME/.config/systemd/user/steam-launcher.service.d"
    local dropin_file="$dropin_dir/navi.conf"
    local env_dir="$HOME/.config/environment.d"
    local env_file="$env_dir/10-navi-path.conf"

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        if [ "$is_steamos" -eq 1 ]; then
            log_dry "Install SteamOS Gaming Mode systemd drop-in at $dropin_file"
        fi
        if [ "$is_steamos" -eq 1 ] || [ -d "/usr/lib/environment.d" ] || [ -d "/etc/environment.d" ]; then
            log_dry "Configure systemd user environment PATH at $env_file"
        fi
        return 0
    fi

    if [ "$is_steamos" -eq 1 ]; then
        mkdir -p "$dropin_dir"
        cat > "$dropin_file" <<EOF
[Service]
Environment="LD_AUDIT=%h/.local/share/navi/library-inject.so:%h/.local/share/navi/libnavi.so"
Environment="PATH=%h/.local/bin:%h/.local/share/navi/path:/usr/local/bin:/usr/bin"
EOF
        if command -v systemctl >/dev/null 2>&1; then
            systemctl --user daemon-reload 2>/dev/null || true
        fi
        log_info "Installed SteamOS Gaming Mode drop-in at $dropin_file"
    fi

    # systemd environment.d path export (ensures systemd user services find ~/.local/bin)
    if [ "$is_steamos" -eq 1 ] || [ -d "/usr/lib/environment.d" ] || [ -d "/etc/environment.d" ]; then
        mkdir -p "$env_dir"
        cat > "$env_file" <<'EOF'
PATH=${HOME}/.local/bin:${HOME}/.local/share/navi/path:${PATH}
EOF
        if command -v systemctl >/dev/null 2>&1; then
            systemctl --user import-environment PATH 2>/dev/null || true
        fi
        log_info "Configured systemd user environment at $env_file"
    fi
}

merge_updates_configuration() {
    local src_res="$1"
    local dest_dir="$HOME/.config/navi"

    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Configure config.yaml and merge updates.yaml in $dest_dir"
        return 0
    fi

    mkdir -p "$dest_dir"

    # config.yaml
    if [ -f "$dest_dir/config.yaml" ]; then
        sed -i 's/^SafeMode: .*/SafeMode: no/' "$dest_dir/config.yaml"
    elif [ -f "$src_res/config.yaml" ]; then
        cp -a "$src_res/config.yaml" "$dest_dir/config.yaml"
        sed -i 's/^SafeMode: .*/SafeMode: no/' "$dest_dir/config.yaml"
    fi

    # Detect local steamclient hash
    local client_ver
    local client_sha
    client_ver=$(grep -oP '"version"\s+"\K[0-9]+' "$HOME/.local/share/Steam/package/steam_client_*.manifest" 2>/dev/null | head -1 || true)
    client_sha=$(sha256sum "$HOME/.local/share/Steam/ubuntu12_32/steamclient.so" 2>/dev/null | cut -d' ' -f1 || true)

    local src_updates="$src_res/updates.yaml"
    local dest_updates="$dest_dir/updates.yaml"

    if [ -f "$src_updates" ]; then
        if [ ! -f "$dest_updates" ]; then
            cp -a "$src_updates" "$dest_updates"
        else
            local ts
            ts="$(date +%Y%m%d-%H%M%S)"
            cp -a "$dest_updates" "$dest_updates.bak-$ts"
            python3 - <<'EOF' "$src_updates" "$dest_updates" "$client_sha" "$client_ver"
import sys, re

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
        fi
    fi
}

do_install() {
    log_info "Starting Navi installation..."

    # 1. Steam running check
    check_steam_running || exit 1

    # 2. Run legacy migration
    if type migration_run_install >/dev/null 2>&1; then
        migration_run_install
    fi

    # Dry-run early exit
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "Install Navi payload into $HOME/.local/share/navi"
        if [ -d "$HOME/.var/app/com.valvesoftware.Steam" ]; then
            log_dry "Install Navi Flatpak payload and environment overrides"
        fi
        install_safe_wrapper
        install_desktop_entries
        install_steamos_systemd_integration
        merge_updates_configuration "$REPO_ROOT/res"
        refresh_system_caches
        log_dry "Navi installation preview complete (0 files modified)"
        return 0
    fi

    # 3. Locate bundled payload
    local bin_dir
    local res_dir
    bin_dir="$(find_payload_bin_dir || true)"
    res_dir="$(find_payload_res_dir || true)"

    if [ -z "$bin_dir" ] || [ ! -f "$bin_dir/libnavi.so" ]; then
        log_error "libnavi.so not found! Please compile or place binaries in bin/ before installing."
        exit 1
    fi

    # 4. Attempt online Headcrab integration if requested/available, fallback gracefully offline
    if [ "${NAVI_OFFLINE_INSTALL:-0}" != "1" ] && [ "${SLS_OFFLINE_INSTALL:-0}" != "1" ] && [ -n "${SLS_HEADCRAB_URL:-}" ]; then
        local headcrab_url="$SLS_HEADCRAB_URL"
        local headcrab_script
        headcrab_script="$(mktemp "${TMPDIR:-/tmp}/headcrab.XXXXXX.sh")"
        if curl -fsSL --connect-timeout 5 "$headcrab_url" -o "$headcrab_script" 2>/dev/null; then
            chmod +x "$headcrab_script"
            bash "$headcrab_script" || log_warn "Headcrab installer exited non-zero; continuing with bundled payload..."
        fi
        rm -f "$headcrab_script"
    fi

    # 5. Install payload to ~/.local/share/navi
    local navi_share="$HOME/.local/share/navi"
    mkdir -p "$navi_share"
    mkdir -p "$navi_share/path"

    if [ -f "$bin_dir/libnavi.so" ]; then
        cp -f --remove-destination "$bin_dir/libnavi.so" "$navi_share/libnavi.so"
    fi
    if [ -f "$bin_dir/library-inject.so" ]; then
        cp -f --remove-destination "$bin_dir/library-inject.so" "$navi_share/library-inject.so"
    fi

    if [ -n "$res_dir" ] && [ -d "$res_dir" ]; then
        mkdir -p "$navi_share/res"
        smart_copy "$res_dir" "$navi_share/res"
    fi

    # Cloud helper binary
    mkdir -p "$navi_share/bin"
    if [ -n "$bin_dir" ] && [ -f "$bin_dir/navi-cloud-apply-pending" ]; then
        cp -f --remove-destination "$bin_dir/navi-cloud-apply-pending" "$navi_share/bin/navi-cloud-apply-pending"
        chmod 755 "$navi_share/bin/navi-cloud-apply-pending"
    elif [ -f "$REPO_ROOT/bin/navi-cloud-apply-pending" ]; then
        cp -f --remove-destination "$REPO_ROOT/bin/navi-cloud-apply-pending" "$navi_share/bin/navi-cloud-apply-pending"
        chmod 755 "$navi_share/bin/navi-cloud-apply-pending"
    fi

    # CloudRedirect payload
    local cr_dir="$navi_share/cloudredirect"
    mkdir -p "$cr_dir"
    local cr_src="${res_dir:-$REPO_ROOT/res}"
    if [ -f "$cr_src/cloud_redirect.so" ]; then
        cp -f --remove-destination "$cr_src/cloud_redirect.so" "$cr_dir/cloud_redirect.so"
        chmod 755 "$cr_dir/cloud_redirect.so"
    elif [ -f "$REPO_ROOT/res/cloud_redirect.so" ]; then
        cp -f --remove-destination "$REPO_ROOT/res/cloud_redirect.so" "$cr_dir/cloud_redirect.so"
        chmod 755 "$cr_dir/cloud_redirect.so"
    fi
    if [ -f "$cr_src/cloud_redirect_cli" ]; then
        cp -f --remove-destination "$cr_src/cloud_redirect_cli" "$cr_dir/cloud_redirect_cli"
        chmod 755 "$cr_dir/cloud_redirect_cli"
    elif [ -f "$REPO_ROOT/res/cloud_redirect_cli" ]; then
        cp -f --remove-destination "$REPO_ROOT/res/cloud_redirect_cli" "$cr_dir/cloud_redirect_cli"
        chmod 755 "$cr_dir/cloud_redirect_cli"
    fi

    # 6. Flatpak Steam support
    local flatpak_steam_dir="$HOME/.var/app/com.valvesoftware.Steam"
    if [ -d "$flatpak_steam_dir" ] || (command -v flatpak >/dev/null 2>&1 && flatpak info com.valvesoftware.Steam >/dev/null 2>&1); then
        local flatpak_navi="$flatpak_steam_dir/.local/share/navi"
        mkdir -p "$flatpak_navi"
        [ -f "$bin_dir/libnavi.so" ] && cp -f --remove-destination "$bin_dir/libnavi.so" "$flatpak_navi/"
        [ -f "$bin_dir/library-inject.so" ] && cp -f --remove-destination "$bin_dir/library-inject.so" "$flatpak_navi/"
        if [ -n "$res_dir" ] && [ -d "$res_dir" ]; then
            mkdir -p "$flatpak_navi/res"
            smart_copy "$res_dir" "$flatpak_navi/res"
        fi

        mkdir -p "$flatpak_navi/bin"
        if [ -n "$bin_dir" ] && [ -f "$bin_dir/navi-cloud-apply-pending" ]; then
            cp -f --remove-destination "$bin_dir/navi-cloud-apply-pending" "$flatpak_navi/bin/navi-cloud-apply-pending"
            chmod 755 "$flatpak_navi/bin/navi-cloud-apply-pending"
        elif [ -f "$REPO_ROOT/bin/navi-cloud-apply-pending" ]; then
            cp -f --remove-destination "$REPO_ROOT/bin/navi-cloud-apply-pending" "$flatpak_navi/bin/navi-cloud-apply-pending"
            chmod 755 "$flatpak_navi/bin/navi-cloud-apply-pending"
        fi

        local fp_cr_dir="$flatpak_navi/cloudredirect"
        mkdir -p "$fp_cr_dir"
        if [ -f "$cr_src/cloud_redirect.so" ]; then
            cp -f --remove-destination "$cr_src/cloud_redirect.so" "$fp_cr_dir/cloud_redirect.so"
            chmod 755 "$fp_cr_dir/cloud_redirect.so"
        elif [ -f "$REPO_ROOT/res/cloud_redirect.so" ]; then
            cp -f --remove-destination "$REPO_ROOT/res/cloud_redirect.so" "$fp_cr_dir/cloud_redirect.so"
            chmod 755 "$fp_cr_dir/cloud_redirect.so"
        fi
        if [ -f "$cr_src/cloud_redirect_cli" ]; then
            cp -f --remove-destination "$cr_src/cloud_redirect_cli" "$fp_cr_dir/cloud_redirect_cli"
            chmod 755 "$fp_cr_dir/cloud_redirect_cli"
        elif [ -f "$REPO_ROOT/res/cloud_redirect_cli" ]; then
            cp -f --remove-destination "$REPO_ROOT/res/cloud_redirect_cli" "$fp_cr_dir/cloud_redirect_cli"
            chmod 755 "$fp_cr_dir/cloud_redirect_cli"
        fi

        if command -v flatpak >/dev/null 2>&1; then
            local fp_audit="/app/links/\$LIB/libshared-library-guard.so:$flatpak_navi/library-inject.so:$flatpak_navi/libnavi.so"
            flatpak override --user --env=LD_AUDIT="$fp_audit" --env=SHARED_LIBRARY_GUARD=0 com.valvesoftware.Steam 2>/dev/null || true
        fi
    fi

    # 7. Safe Steam wrapper & PATH
    install_safe_wrapper

    # 8. Desktop integration
    install_desktop_entries

    # 9. SteamOS Gaming Mode & systemd integration
    install_steamos_systemd_integration

    # 10. Configuration & updates.yaml
    if [ -n "$res_dir" ]; then
        merge_updates_configuration "$res_dir"
    fi

    # 11. Refresh caches
    refresh_system_caches

    log_info "Navi installation completed successfully!"
}
