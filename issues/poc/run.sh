#!/usr/bin/env bash
#
# Regression runner for issues/ proof-of-concept reproducers.
#
# Re-runs every PoC under issues/poc/<component>/ and checks it still behaves as
# recorded, so fixed bugs can be confirmed not to have regressed in one shot.
#
# Usage:
#   issues/poc/run.sh [component]      # default: all components
#
# IMPORTANT: rebuild the compiler first (so bin/Orbit reflects current code).
#
# PoC kinds:
#   *.orb  source reproducer. First "# EXPECT:" line declares the expectation:
#            # EXPECT: ok                  -> compiles cleanly (exit 0)
#            # EXPECT: error               -> compilation fails (exit != 0)
#            # EXPECT: error: <substring>  -> fails AND output contains <substring>
#            # EXPECT: contains: <substr>  -> runs (exit 0) AND output contains
#                                            <substr> (for runtime tests that
#                                            print a success marker)
#   *.cpp  self-checking byte-level probe linked against libOrbiter. Must exit 0
#          on success (it prints its own per-case PASS/FAIL).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Locate the build tree (artifacts live in <build>/bin and <build>/lib).
# ORBIT_BUILD_DIR wins (ctest sets it); otherwise pick the first preset/CLion
# build dir that contains a built Orbit.
if [ -z "${ORBIT_BUILD_DIR:-}" ]; then
    for d in "$ROOT/build/dev" "$ROOT/build/debug" "$ROOT/build/release" "$ROOT/cmake-build-debug"; do
        if [ -x "$d/bin/Orbit" ]; then ORBIT_BUILD_DIR="$d"; break; fi
    done
fi
if [ -z "${ORBIT_BUILD_DIR:-}" ] || [ ! -x "$ORBIT_BUILD_DIR/bin/Orbit" ]; then
    echo "poc: no built Orbit found; build first, or set ORBIT_BUILD_DIR" >&2
    exit 2
fi
ORBIT="$ORBIT_BUILD_DIR/bin/Orbit"
export ORBIT_PATH="$ROOT/stdlib"

# A reproducer for a hang must not hang the suite: cap every run. Override with
# POC_TIMEOUT=<seconds> for a slow machine. `timeout` exits 124 on expiry.
POC_TIMEOUT="${POC_TIMEOUT:-30}"

filter="${1:-}"
pass=0; fail=0

ok_line()  { pass=$((pass+1)); printf '  PASS  %s\n' "$1"; }
bad_line() { fail=$((fail+1)); printf '  FAIL  %s\n      %s\n' "$1" "$2"; }

run_orb() {
    local f="$1" rel="$2"
    local line expect sub out rc
    line="$(sed -n 's/.*EXPECT:[[:space:]]*//p' "$f" | head -1 | tr -d '\r')"
    if [ -z "$line" ]; then bad_line "$rel" "no '# EXPECT:' directive"; return; fi

    out="$(timeout "$POC_TIMEOUT" "$ORBIT" "$f" 2>&1)"; rc=$?

    if [ $rc -eq 124 ]; then
        bad_line "$rel" "timed out after ${POC_TIMEOUT}s (hang)"; return
    fi

    case "$line" in
        ok)
            if [ $rc -eq 0 ]; then ok_line "$rel"
            else bad_line "$rel" "expected ok, exit=$rc: $(echo "$out" | grep -iE 'invalid|error|assert' | head -1)"; fi ;;
        error*)
            sub="$(echo "$line" | sed -n 's/^error:[[:space:]]*//p')"
            if [ $rc -eq 0 ]; then bad_line "$rel" "expected error, compiled ok"
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

run_cpp() {
    local f="$1" rel="$2"
    local bin="/tmp/poc_$(basename "$f" .cpp)"
    # Headers: the source root (#include <orbit/...>), the build tree (generated
    # version.h) and stratum. Stratum is folded into libOrbiter, so only
    # -lOrbiter is needed.
    if ! clang++ -std=c++17 -I "$ROOT" -I "$ORBIT_BUILD_DIR/include" -I "$ROOT/lib/stratum" "$f" \
            -L "$ORBIT_BUILD_DIR/lib" -lOrbiter -Wl,-rpath,"$ORBIT_BUILD_DIR/lib" -o "$bin" \
            2>/tmp/poc_build.log; then
        bad_line "$rel" "build failed: $(grep -i error /tmp/poc_build.log | head -1)"; return
    fi
    if "$bin" >/tmp/poc_run.log 2>&1; then ok_line "$rel"
    else bad_line "$rel" "self-check failed:\n$(sed 's/^/        /' /tmp/poc_run.log)"; fi
}

[ -x "$ORBIT" ] || { echo "error: $ORBIT not found — build the project first." >&2; exit 2; }

for dir in "$SCRIPT_DIR"/*/; do
    comp="$(basename "$dir")"
    [ -n "$filter" ] && [ "$filter" != "$comp" ] && continue
    echo "== $comp =="
    for f in "$dir"*.orb "$dir"*.cpp; do
        [ -e "$f" ] || continue
        rel="$comp/$(basename "$f")"
        case "$f" in
            *.orb) run_orb "$f" "$rel" ;;
            *.cpp) run_cpp "$f" "$rel" ;;
        esac
    done
done

echo
echo "total: $((pass+fail))  pass: $pass  fail: $fail"
[ "$fail" -eq 0 ]
