#!/bin/bash
# Runs a command on the real Qt Wayland plugin, on a private compositor with its own bare
# D-Bus session. Nothing appears on the desktop, and the real session isn't touched.
# It has no pointer or keyboard, so Wayland refuses popups there.
#
# Usage: tools/headless-wayland.sh <command> [arguments]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)

if [ "${FG_HEADLESS_INSIDE:-}" != 1 ]; then
    FG_HEADLESS_INSIDE=1 exec timeout -k 3 "${FG_HEADLESS_SECONDS:-120}" \
        dbus-run-session --config-file="$HERE/headless-bus.conf" -- "$0" "$@" </dev/null
fi

# Checked before the compositor starts, so that stopping here leaves nothing running.
: "${XDG_RUNTIME_DIR:?XDG_RUNTIME_DIR must be set.}"
SOCKET=fg-headless-$$
kwin_wayland --virtual --no-lockscreen --no-global-shortcuts --no-kactivities --socket "$SOCKET" --width 1400 --height 1000 >/dev/null 2>&1 &
COMPOSITOR=$!
for _ in $(seq 1 50); do
    [ -S "$XDG_RUNTIME_DIR/$SOCKET" ] && break
    sleep 0.1
done
if [ ! -S "$XDG_RUNTIME_DIR/$SOCKET" ]; then
    echo "The private compositor did not start." >&2
    kill "$COMPOSITOR" 2>/dev/null
    exit 3
fi

unset DISPLAY
WAYLAND_DISPLAY=$SOCKET QT_QPA_PLATFORM=wayland QT_FORCE_STDERR_LOGGING=1 QT_NO_XDG_DESKTOP_PORTAL=1 "$@"
RESULT=$?
kill "$COMPOSITOR" 2>/dev/null
wait "$COMPOSITOR" 2>/dev/null
exit "$RESULT"
