#!/usr/bin/env bash
# scripts/lib/test.sh - Test suite runner module for Navi
set -u

do_test() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make test"
        return 0
    fi

    log_info "Running test suite via make test..."
    make -C "$REPO_ROOT" test
}
