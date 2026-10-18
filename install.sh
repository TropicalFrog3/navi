#!/usr/bin/env bash
# install.sh - Navi unified management and installation dispatcher
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export REPO_ROOT="$SCRIPT_DIR"

usage() {
    cat << 'EOF'
Usage: ./install.sh [SUBCOMMAND] [OPTIONS]

Unified management tool for Navi.

Subcommands:
  install      Install Navi payload and configure Steam integration (default)
  uninstall    Completely remove Navi and restore native Steam launchers
  update       Update installed Navi payload and configuration
  build        Build Navi binaries from source
  release      Build and package production release bundle
  test         Execute test suite (make test)

Options:
  --yes, -y     Automatic yes to prompts; run non-interactively
  --dry-run, -n Preview actions without making changes on disk
  --help, -h    Display this help message and exit
EOF
}

SUBCOMMAND=""
ASSUME_YES=0
DRY_RUN=0

# Parse command line options and subcommand
while [ $# -gt 0 ]; do
    case "$1" in
        install|uninstall|update|build|release|test)
            if [ -n "$SUBCOMMAND" ]; then
                echo "Error: Multiple subcommands specified: $SUBCOMMAND and $1" >&2
                usage >&2
                exit 1
            fi
            SUBCOMMAND="$1"
            ;;
        --yes|-y)
            ASSUME_YES=1
            ;;
        --dry-run|-n)
            DRY_RUN=1
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

# Default subcommand is install
SUBCOMMAND="${SUBCOMMAND:-install}"

export ASSUME_YES
export DRY_RUN

# Source common helpers
# shellcheck source=scripts/lib/common.sh
source "$REPO_ROOT/scripts/lib/common.sh"

# Source legacy migration helper
if [ -f "$REPO_ROOT/scripts/lib/migration.sh" ]; then
    # shellcheck source=scripts/lib/migration.sh
    source "$REPO_ROOT/scripts/lib/migration.sh"
fi

# Dispatch to appropriate module
case "$SUBCOMMAND" in
    install)
        # shellcheck source=scripts/lib/install.sh
        source "$REPO_ROOT/scripts/lib/install.sh"
        do_install
        ;;
    uninstall)
        # shellcheck source=scripts/lib/uninstall.sh
        source "$REPO_ROOT/scripts/lib/uninstall.sh"
        do_uninstall
        ;;
    update)
        # shellcheck source=scripts/lib/update.sh
        source "$REPO_ROOT/scripts/lib/update.sh"
        do_update
        ;;
    build)
        # shellcheck source=scripts/lib/build.sh
        source "$REPO_ROOT/scripts/lib/build.sh"
        do_build
        ;;
    release)
        # shellcheck source=scripts/lib/release.sh
        source "$REPO_ROOT/scripts/lib/release.sh"
        do_release
        ;;
    test)
        # shellcheck source=scripts/lib/test.sh
        source "$REPO_ROOT/scripts/lib/test.sh"
        do_test
        ;;
    *)
        log_error "Unknown subcommand: $SUBCOMMAND"
        usage >&2
        exit 1
        ;;
esac
