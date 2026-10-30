#!/bin/sh
# Measures this app's own cost per view: CPU over a timed window, and memory. --start-test times the start.
# Usage: tools/measure-idle.sh <binary> [seconds per view]
# Runs without a visible window, so these figures leave out the cost of real compositing.
set -eu
BIN=${1:?usage: measure-idle.sh <binary> [seconds]}
SECONDS_PER_VIEW=${2:-20}
WORK=$(mktemp -d "${FG_WORK:-$PWD/scratchpad}/measure.XXXXXX")
TICKS=$(getconf CLK_TCK)

ticks_of() { awk '{ print $14 + $15 }' "/proc/$1/stat"; }
rss_of() { awk '/^VmRSS/ { print $2 }' "/proc/$1/status"; }
own_of() { awk '/^RssAnon/ { print $2 }' "/proc/$1/status"; }
switches_of() { awk '/ctxt_switches/ { n += $2 } END { print n }' "/proc/$1/status"; }

printf '%-5s %10s %10s %14s %12s\n' view 'cpu %' 'rss MB' 'not shared MB' 'wakeups/s'
for view in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
    XDG_CONFIG_HOME=$WORK/cfg XDG_DATA_HOME=$WORK/data QT_QPA_PLATFORM=offscreen "$BIN" --view "$view" &
    PID=$!
    sleep 3 # settle
    T0=$(ticks_of "$PID"); W0=$(switches_of "$PID")
    sleep "$SECONDS_PER_VIEW"
    T1=$(ticks_of "$PID"); W1=$(switches_of "$PID"); RSS=$(rss_of "$PID"); OWN=$(own_of "$PID")
    kill "$PID"; wait "$PID" 2>/dev/null || true
    awk -v v="$view" -v t=$((T1 - T0)) -v hz="$TICKS" -v s="$SECONDS_PER_VIEW" -v r="$RSS" -v o="$OWN" -v w=$((W1 - W0)) \
        'BEGIN { printf "%-5s %10.2f %10.1f %14.1f %12.1f\n", v, 100 * t / hz / s, r / 1024, o / 1024, w / s }'
done
