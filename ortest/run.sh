#!/usr/bin/env bash
#
# Acceptance / regression runner for the ortest/ suites.
#
# Runs every reusable feature test under ortest/<topic>/ and checks it still
# behaves as recorded. Intended to be run on every build of the interpreter,
# especially before a production build -- all suites must be green to ship.
#
# This mirrors issues/poc/run.sh but serves a different role: poc/ holds bug
# reproducers bound to tracked finding IDs ("did a known bug come back?"), while
# ortest/ holds broad behavioral suites grouped by topic ("does feature X still
# work?"). New topics are added as new subfolders.
#
# Usage:
#   ortest/run.sh [topic]      # default: all topics; e.g. `ortest/run.sh regalloc`
#
# IMPORTANT: rebuild the compiler first (so bin/Orbit reflects current code).
#
# Each *.orb declares its expectation in a "# EXPECT:" line:
#   # EXPECT: ok                  -> compiles and runs cleanly (exit 0)
#   # EXPECT: error               -> fails (exit != 0)
#   # EXPECT: error: <substring>  -> fails AND output contains <substring>
#   # EXPECT: contains: <substr>  -> runs (exit 0) AND output contains <substr>
#                                    (the usual form: a suite prints a success
#                                    marker such as "ALL TESTS PASSED")
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Locate the build tree (artifacts live in <build>/bin and <build>/lib).
# ORBIT_BUILD_DIR wins (ctest sets it); otherwise pick the first preset/CLion
# build dir that contains a built Orbit.
if [ -z "${ORBIT_BUILD_DIR:-}" ]; then
    for d in "$ROOT/build/dev" "$ROOT/build/debug" "$ROOT/build/release" "$ROOT/cmake-build-debug"; do
        if [ -x "$d/bin/Orbit" ]; then ORBIT_BUILD_DIR="$d"; break; fi
    done
fi
if [ -z "${ORBIT_BUILD_DIR:-}" ] || [ ! -x "$ORBIT_BUILD_DIR/bin/Orbit" ]; then
    echo "ortest: no built Orbit found; build first, or set ORBIT_BUILD_DIR" >&2
    exit 2
fi
ORBIT="$ORBIT_BUILD_DIR/bin/Orbit"
export ORBIT_PATH="$ROOT/stdlib"

# A suite that hangs must not hang the gate: cap every run. Override with
# ORTEST_TIMEOUT=<seconds> on a slow machine. `timeout` exits 124 on expiry.
ORTEST_TIMEOUT="${ORTEST_TIMEOUT:-30}"

filter="${1:-}"
pass=0; fail=0

ok_line()  { pass=$((pass+1)); printf '  PASS  %s\n' "$1"; }
bad_line() { fail=$((fail+1)); printf '  FAIL  %s\n      %s\n' "$1" "$2"; }

run_orb() {
    local f="$1" rel="$2"
    local line sub out rc
    line="$(sed -n 's/.*EXPECT:[[:space:]]*//p' "$f" | head -1 | tr -d '\r')"
    if [ -z "$line" ]; then bad_line "$rel" "no '# EXPECT:' directive"; return; fi

    out="$(timeout "$ORTEST_TIMEOUT" "$ORBIT" "$f" 2>&1)"; rc=$?

    if [ $rc -eq 124 ]; then
        bad_line "$rel" "timed out after ${ORTEST_TIMEOUT}s (hang)"; return
    fi

    case "$line" in
        ok)
            if [ $rc -eq 0 ]; then ok_line "$rel"
            else bad_line "$rel" "expected ok, exit=$rc: $(echo "$out" | grep -iE 'invalid|error|assert' | head -1)"; fi ;;
        error*)
            sub="$(echo "$line" | sed -n 's/^error:[[:space:]]*//p')"
            if [ $rc -eq 0 ]; then bad_line "$rel" "expected error, ran ok"
            elif [ -n "$sub" ] && ! echo "$out" | grep -qiF "$sub"; then
                bad_line "$rel" "errored but message lacked '$sub'"
            else ok_line "$rel"; fi ;;
        contains:*)
            sub="$(echo "$line" | sed -n 's/^contains:[[:space:]]*//p')"
            if [ $rc -ne 0 ]; then
                bad_line "$rel" "expected a successful run, exit=$rc: $(echo "$out" | grep -iE 'invalid|error|assert' | head -1)"
            elif ! echo "$out" | grep -qF "$sub"; then
                bad_line "$rel" "ran but output lacked '$sub'"
            else ok_line "$rel"; fi ;;
        *)
            bad_line "$rel" "unknown EXPECT '$line'" ;;
    esac
}

[ -x "$ORBIT" ] || { echo "error: $ORBIT not found -- build the project first." >&2; exit 2; }

for dir in "$SCRIPT_DIR"/*/; do
    topic="$(basename "$dir")"
    [ -n "$filter" ] && [ "$filter" != "$topic" ] && continue
    echo "== $topic =="
    for f in "$dir"*.orb; do
        [ -e "$f" ] || continue
        run_orb "$f" "$topic/$(basename "$f")"
    done
done

echo
echo "total: $((pass+fail))  pass: $pass  fail: $fail"
[ "$fail" -eq 0 ]
