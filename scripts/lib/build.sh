#!/usr/bin/env bash
# scripts/lib/build.sh - Build module for Navi
set -u

do_build() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make -C $REPO_ROOT"
        return 0
    fi

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
