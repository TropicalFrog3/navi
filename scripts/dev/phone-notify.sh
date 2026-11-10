#!/usr/bin/env bash
# @file  phone-notify.sh
# @brief Sends build and test completion notifications via webhook
# ==============================================================================
# phone-notify.sh: ADB Wireless/USB Phone Notification & Status CLI
# ==============================================================================
set -euo pipefail

CONFIG_FILE="${HOME}/.config/phone-notify.conf"
DEFAULT_PHONE="${PHONE_DEVICE:-192.168.40.86:5555}"

if [ -f "$CONFIG_FILE" ]; then
    TARGET_PHONE="$(tr -d ' \n\r' < "$CONFIG_FILE")"
else
    TARGET_PHONE="$DEFAULT_PHONE"
fi

SPECIFIED_DEVICE=""
TITLE=""
MESSAGE=""
TAG="goal_notification"
STYLE="bigtext"
WAKE_SCREEN=0

usage() {
    cat << EOF
Phone ADB Notification CLI
Usage: $(basename "$0") [options] [message]
       $(basename "$0") <command> [args]

Commands:
  status                     Show phone status (model, Android version, battery, screen)
  connect [ip[:port]]        Connect ADB to phone (defaults to configured IP)
  set-phone <ip[:port]>      Save persistent default phone IP in ~/.config/phone-notify.conf
  test                       Send a test notification to the phone

Options:
  -t, --title <title>        Notification title (default: "Goal Completed")
  -m, --message <message>    Notification body text
  -w, --wake                 Wake device screen upon notification
  -s, --device <serial>      Specify ADB target device serial
      --tag <tag>            Notification tag (default: "goal_notification")
  -h, --help                 Show this help message

Examples:
  $(basename "$0") "Build and tests completed successfully!"
  $(basename "$0") -t "CR7 Gate Done" -m "All 23 gates verified." -w
  $(basename "$0") status
EOF
}

resolve_device() {
    if [ -n "$SPECIFIED_DEVICE" ]; then
        echo "$SPECIFIED_DEVICE"
        return 0
    fi

    # Check if target is already among connected devices
    local attached
    attached=$(adb devices | awk '$2 == "device" {print $1}')

    for dev in $attached; do
        if [ "$dev" = "$TARGET_PHONE" ]; then
            echo "$dev"
            return 0
        fi
    done

    # If another device is connected, use the first one
    local first_dev
    first_dev=$(echo "$attached" | head -n1)
    if [ -n "$first_dev" ]; then
        echo "$first_dev"
        return 0
    fi

    # Attempt connection to target
    if [ -n "$TARGET_PHONE" ]; then
        echo "[phone-notify] Connecting to $TARGET_PHONE..." >&2
        adb connect "$TARGET_PHONE" >/dev/null 2>&1 || true
        first_dev=$(adb devices | awk '$2 == "device" {print $1; exit}')
        if [ -n "$first_dev" ]; then
            echo "$first_dev"
            return 0
        fi
    fi

    echo "[phone-notify] Error: No Android phone connected via ADB." >&2
    echo "Run 'adb devices' or '$(basename "$0") connect <ip:port>' to connect." >&2
    return 1
}

escape_str() {
    # Escape single quotes for bash passing inside adb shell single quotes
    printf '%s' "$1" | sed "s/'/'\\\\''/g"
}

cmd_status() {
    local dev
    dev=$(resolve_device)
    echo "=== Android Phone Status ($dev) ==="
    local model release battery_level screen_state
    model=$(adb -s "$dev" shell getprop ro.product.model 2>/dev/null | tr -d '\r')
    release=$(adb -s "$dev" shell getprop ro.build.version.release 2>/dev/null | tr -d '\r')
    battery_level=$(adb -s "$dev" shell dumpsys battery 2>/dev/null | awk -F': ' '/level:/ {print $2; exit}' | tr -d '\r')
    screen_state=$(adb -s "$dev" shell dumpsys power 2>/dev/null | awk -F'=' '/mWakefulness=/ {print $2; exit}' | tr -d '\r')

    echo "Model:       ${model:-Unknown}"
    echo "Android:     ${release:-Unknown}"
    echo "Battery:     ${battery_level:-Unknown}%"
    echo "Display:     ${screen_state:-Unknown}"
    echo "Target Host: $TARGET_PHONE"
}

cmd_connect() {
    local host="${1:-$TARGET_PHONE}"
    if [ -z "$host" ]; then
        echo "Usage: $(basename "$0") connect <ip[:port]>" >&2
        exit 1
    fi
    echo "Connecting to $host..."
    adb connect "$host"
}

cmd_set_phone() {
    local host="${1:-}"
    if [ -z "$host" ]; then
        echo "Usage: $(basename "$0") set-phone <ip[:port]>" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$CONFIG_FILE")"
    echo "$host" > "$CONFIG_FILE"
    echo "Default phone set to: $host"
}

post_notification() {
    local dev
    dev=$(resolve_device)

    local title="${TITLE:-Goal Completed}"
    local body="${MESSAGE:-Task finished successfully.}"

    if [ "$WAKE_SCREEN" -eq 1 ]; then
        adb -s "$dev" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1 || true
    fi

    local esc_title esc_body esc_tag
    esc_title=$(escape_str "$title")
    esc_body=$(escape_str "$body")
    esc_tag=$(escape_str "$TAG")

    local cmd="cmd notification post -S $STYLE -t '$esc_title' '$esc_tag' '$esc_body'"
    adb -s "$dev" shell "$cmd" >/dev/null 2>&1

    echo "[phone-notify] Notification sent to $dev: '$title' - '$body'"
}

# Subcommands
if [ $# -gt 0 ]; then
    case "$1" in
        status)
            cmd_status
            exit 0
            ;;
        connect)
            shift
            cmd_connect "${1:-}"
            exit 0
            ;;
        set-phone)
            shift
            cmd_set_phone "${1:-}"
            exit 0
            ;;
        test)
            TITLE="Antigravity Test"
            MESSAGE="Test notification from $(hostname) at $(date '+%H:%M:%S')"
            WAKE_SCREEN=1
            post_notification
            exit 0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
    esac
fi

# Parse flag arguments
POSITIONAL=()
while [ $# -gt 0 ]; do
    case "$1" in
        -t|--title)
            TITLE="$2"
            shift 2
            ;;
        -m|--message)
            MESSAGE="$2"
            shift 2
            ;;
        -s|--device)
            SPECIFIED_DEVICE="$2"
            shift 2
            ;;
        --tag)
            TAG="$2"
            shift 2
            ;;
        -w|--wake)
            WAKE_SCREEN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            POSITIONAL+=("$1")
            shift
            ;;
    esac
done

if [ -z "$MESSAGE" ] && [ ${#POSITIONAL[@]} -gt 0 ]; then
    MESSAGE="${POSITIONAL[*]}"
fi

post_notification

