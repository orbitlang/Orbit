// PoC SCAN-010 — escapes are processed for normal and plain byte strings, but
// NOT for raw strings (r"...") or any hash-delimited form (b#"..."#, r#"..."#).
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

static void expect_bytes(orbiter::Isolate *iso, const char *label, const char *src,
                         const unsigned char *exp, unsigned exp_len) {
    Scanner sc(iso, src);
    Token tk;
    bool ok = sc.NextToken(&tk);

    bool pass = ok && tk.length == exp_len && memcmp(tk.buffer, exp, exp_len) == 0;
    printf("%-22s %s\n", label, pass ? "PASS" : "FAIL");
    if (!pass) {
        failures++;
        printf("    got ok=%d len=%u [", ok, tk.length);
        for (unsigned i = 0; i < tk.length; i++) printf("%02X ", tk.buffer[i]);
        printf("] expected [");
        for (unsigned i = 0; i < exp_len; i++) printf("%02X ", exp[i]);
        printf("]\n");
    }
}

int main() {
    auto *iso = probe::isolate();

    const unsigned char esc[]  = {0x61, 0x0A, 0x62};             // a <LF> b
    const unsigned char raw[]  = {0x61, 0x5C, 0x6E, 0x62};       // a \ n b
    const unsigned char nl[]   = {0x0A};

    // Escapes processed:
    expect_bytes(iso, "normal \"a\\nb\"",  "\"a\\nb\"",   esc, 3);
    expect_bytes(iso, "byte   b\"a\\nb\"", "b\"a\\nb\"",  esc, 3);
    expect_bytes(iso, "byte   b\"\\n\"",   "b\"\\n\"",    nl,  1);

    // Raw (verbatim backslash):
    expect_bytes(iso, "raw    r\"a\\nb\"",   "r\"a\\nb\"",     raw, 4);
    expect_bytes(iso, "byte#  b#\"a\\nb\"#", "b#\"a\\nb\"#",   raw, 4);
    expect_bytes(iso, "raw#   r#\"a\\nb\"#", "r#\"a\\nb\"#",   raw, 4);

    return failures == 0 ? 0 : 1;
}
