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
    log_info "Navi build completed successfully."
}
