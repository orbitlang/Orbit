// PoC IBUF-006 — AppendInput must reclaim consumed input (compaction) and grow
// geometrically, without corrupting data across append/consume cycles.
// EXPECT: exit 0 (self-checking). Build/run via issues/poc/run.sh.
//
// The interactive REPL path (AppendInput + Peek) is not exercised by the .orb
// PoCs (those drive the file-mode ReadFile path), so this probe covers it
// directly: many small lines are appended one at a time and fully consumed,
// which on every iteration after the first runs the "b_cur_ > 0" compaction
// reset. A corruption or off-by-one there would show up as a byte mismatch.
// (Bounded memory itself is verified by inspection — b_capacity_ is private.)
#include <cstdio>

#include <orbit/liftoff/scanner/ibuffer.h>

#include "../probe.h"

using namespace liftoff::scanner;

int main() {
    auto *iso = probe::isolate();

    InputBuffer ib(iso, 64); // small capacity hint -> exercises reuse, not endless growth
    int failures = 0;
    char line[64];

    for (int i = 0; i < 2000 && failures == 0; i++) {
        const int n = snprintf(line, sizeof(line), "line-%d\n", i);

        if (!ib.AppendInput((const unsigned char *) line, n)) {
            printf("append %d FAIL\n", i);
            failures++;
            break;
        }

        // Consume exactly the bytes just appended and verify each one.
        for (int j = 0; j < n; j++) {
            const int c = ib.Peek(true);
            if (c != (unsigned char) line[j]) {
                printf("mismatch line %d byte %d: got %d want %d\n", i, j, c, (unsigned char) line[j]);
                failures++;
                break;
            }
        }

        // Buffer must now be exhausted (Peek returns -1) before the next append.
        if (failures == 0 && ib.Peek(false) != -1) {
            printf("line %d not exhausted after consume\n", i);
            failures++;
        }
    }

    printf("append/consume cycles: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
