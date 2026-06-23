// PoC SBUF-001 — geometric growth + shrink-to-fit must build large tokens
// without O(n^2) blowup AND without corrupting content or losing exact sizing.
// EXPECT: exit 0 (self-checking). Build/run via issues/poc/run.sh.
//
// This is a correctness guard for the growth/finalize logic (the perf property
// itself isn't asserted here): it builds a large buffer byte-by-byte through the
// same PutChar path the scanner uses, then checks length, NUL termination, and
// every byte after the hand-off (which runs the shrink-to-fit realloc).

#include <cstdio>

#include <orbit/liftoff/scanner/sbuffer.h>

#include "../probe.h"

using namespace liftoff::scanner;

static int failures = 0;

static void check(const char *label, bool cond) {
    printf("  %-32s %s\n", label, cond ? "PASS" : "FAIL");
    if (!cond) failures++;
}

int main() {
    auto *iso = probe::isolate();

    const unsigned N = 5000; // well past Stratum's 1024 block-max, exercises both regimes

    StoreBuffer sb(iso);
    for (unsigned i = 0; i < N; i++) {
        if (!sb.PutChar((unsigned char) (i % 251))) {
            check("PutChar succeeded", false);
            return 1;
        }
    }

    unsigned char *buf = nullptr;
    unsigned len = sb.GetBuffer(&buf);

    check("length is exact", len == N);
    check("buffer non-null", buf != nullptr);
    check("NUL terminated", buf != nullptr && buf[N] == '\0');

    bool intact = buf != nullptr;
    for (unsigned i = 0; intact && i < N; i++)
        intact = buf[i] == (unsigned char) (i % 251);
    check("content intact after grow+shrink", intact);

    return failures == 0 ? 0 : 1;
}
