#!/usr/bin/env bash
# @file  build.sh
# @brief Build pipeline compiling C++ engine, AutoCrack, and bundling scripts
# scripts/lib/build.sh - Build module for Navi
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"

embed_config() {
    local header="$REPO_ROOT/src/model/config_default.hpp"
    local config_yaml="$REPO_ROOT/res/config.yaml"
    local default_config
    default_config="$(cat "$config_yaml")"
    local current_header=""
    [ -f "$header" ] && current_header="$(cat "$header")"

    local new_embedded="#pragma once

constexpr static const char* defaultConfig = R\"($default_config)\";"

    if [ "$current_header" != "$new_embedded" ]; then
        echo "$new_embedded" > "$header"
    fi
}

embed_version() {
    local header="$REPO_ROOT/src/core/version.hpp"
    local version_file="$REPO_ROOT/res/version"
    local version
    version="$(grep '^Version:' "$version_file" | cut -d' ' -f2 | xargs)"
    local current_header=""
    [ -f "$header" ] && current_header="$(cat "$header")"

    local new_embedded="#pragma once

#include <cstdint>


constexpr const char* VERSION = \"$version\";"

    if [ "$current_header" != "$new_embedded" ]; then
        echo "$new_embedded" > "$header"
    fi
}

do_build() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make -C $REPO_ROOT"
        return 0
    fi

    log_info "Embedding version and configuration..."
    embed_config
    embed_version

    log_info "Building Navi from source..."
    cd "$REPO_ROOT"
    if ! make; then
        if [ -f "$REPO_ROOT/docker/build.sh" ]; then
            log_warn "Host build failed, falling back to docker build..."
            "$REPO_ROOT/docker/build.sh"
        else
            log_error "Build failed."
            return 1
        fi
    fi
    if [ -f "$REPO_ROOT/third_party/SteamAutoCrack/SteamAutoCrack.CLI/SteamAutoCrack.CLI.csproj" ]; then
        if command -v dotnet >/dev/null 2>&1; then
            log_info "Building AutoCrack payload..."
            make autocrack || log_warn "AutoCrack build failed; continuing..."
        else
            log_warn "dotnet SDK not found; skipping AutoCrack build."
        fi
    fi
    log_info "Navi build completed successfully."
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    case "${1:-}" in
        embed-config)
            embed_config
            ;;
        embed-version)
            embed_version
            ;;
        embed)
            embed_config
            embed_version
            ;;
        *)
            if [ -f "$REPO_ROOT/scripts/lib/common.sh" ]; then
                # shellcheck source=scripts/lib/common.sh
                source "$REPO_ROOT/scripts/lib/common.sh"
            fi
            do_build
            ;;
    esac
fi
