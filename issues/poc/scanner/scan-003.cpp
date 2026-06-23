// PoC SCAN-003 — octal escapes decode correctly and overflow is rejected.
// EXPECT: exit 0 (self-checking). Build/run via issues/poc/run.sh.
//
// Byte-level checks need the scanner directly: the CLI stops before running
// imports, so it cannot print the resulting string bytes.
#include <cstdio>
#include <cstring>

#include <orbit/liftoff/scanner/scanner.h>

#include "../probe.h"

using namespace liftoff::scanner;

static int failures = 0;

// Scan one token from `src`; assert it succeeds and its buffer equals `exp`.
static void expect_bytes(orbiter::Isolate *iso, const char *label, const char *src,
                         const unsigned char *exp, unsigned exp_len) {
    Scanner sc(iso, src);
    Token tk;
    bool ok = sc.NextToken(&tk);

    bool pass = ok && tk.length == exp_len && memcmp(tk.buffer, exp, exp_len) == 0;
    printf("%-24s %s\n", label, pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
        printf("    got ok=%d len=%u [", ok, tk.length);
        for (unsigned i = 0; i < tk.length; i++) printf("%02X ", tk.buffer[i]);
        printf("] expected [");
        for (unsigned i = 0; i < exp_len; i++) printf("%02X ", exp[i]);
        printf("]\n");
    }
}

// Scan one token; assert it FAILS with the given status.
static void expect_error(orbiter::Isolate *iso, const char *label, const char *src,
                         ScannerStatus want) {
    Scanner sc(iso, src);
    Token tk;
    bool ok = sc.NextToken(&tk);

    bool pass = !ok && sc.status == want;
    printf("%-24s %s\n", label, pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
        printf("    got ok=%d status=%d (%s)\n", ok, (int) sc.status, sc.GetStatusMessage());
    }
}

int main() {
    auto *iso = probe::isolate();

    // "\7\50\100\101" -> 0x07, 0x28 (\50=40), 0x40 (\100=64), 0x41 (\101=65)
    const unsigned char exp[] = {0x07, 0x28, 0x40, 0x41};
    expect_bytes(iso, "octal \\7\\50\\100\\101", "\"\\7\\50\\100\\101\"", exp, 4);

    // "\400" -> octal 256, out of byte range -> INVALID_OCT_BYTE
    expect_error(iso, "octal overflow \\400", "\"\\400\"", ScannerStatus::INVALID_OCT_BYTE);

    return failures == 0 ? 0 : 1;
}
