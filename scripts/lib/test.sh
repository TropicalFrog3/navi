#!/usr/bin/env bash
# @file  test.sh
# @brief Test runner executing unit tests, integration gates, and coverage
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

do_test_gate07() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make test-gate07"
        return 0
    fi

    log_info "Running Gate 0.7 integration harness via make test-gate07..."
    make -C "$REPO_ROOT" test-gate07
}

do_test_ui() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make test-ui-p0"
        return 0
    fi

    log_info "Running UI journey tests via make test-ui-p0..."
    make -C "$REPO_ROOT" test-ui-p0
}

do_test_flow() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make test-flow"
        return 0
    fi

    log_info "Running full flow test via make test-flow..."
    make -C "$REPO_ROOT" test-flow
}

do_check_cloudredirect() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make check-cloudredirect"
        return 0
    fi

    log_info "Checking CloudRedirect integration via make check-cloudredirect..."
    make -C "$REPO_ROOT" check-cloudredirect
}

do_coverage() {
    if [ "${DRY_RUN:-0}" -eq 1 ]; then
        log_dry "make coverage"
        return 0
    fi

    log_info "Running code coverage via make coverage..."
    make -C "$REPO_ROOT" coverage
}
