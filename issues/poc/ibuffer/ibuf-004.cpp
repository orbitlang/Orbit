// PoC IBUF-004 — an I/O read error must surface as IO_ERROR (a distinct status),
// not as EOF and not as NOMEM.
// EXPECT: exit 0 (self-checking). Build/run via issues/poc/run.sh.
//
// Trigger: opening a directory as a file makes fread() fail with the stream's
// error indicator set (EISDIR on Unix). The scanner must report IO_ERROR.
#include <cstdio>

#include <orbit/liftoff/scanner/scanner.h>

#include "../probe.h"

using namespace liftoff::scanner;

int main() {
    auto *iso = probe::isolate();

    FILE *fd = fopen("/", "rb"); // a directory: fread() fails with ferror
    if (fd == nullptr) {
        printf("SKIP — could not open a directory as FILE*\n");
        return 0; // environment can't reproduce; don't fail the suite
    }

    Scanner sc(iso, fd, nullptr, nullptr); // file mode, no prompt -> ReadFile path
    Token tk;
    bool ok = sc.NextToken(&tk);
    fclose(fd);

    bool pass = !ok && sc.status == ScannerStatus::IO_ERROR;
    printf("I/O read error -> %-8s %s\n",
           pass ? "IO_ERROR" : sc.GetStatusMessage(), pass ? "PASS" : "FAIL");
    if (!pass)
        printf("    got ok=%d status=%d (%s)\n", ok, (int) sc.status, sc.GetStatusMessage());

    return pass ? 0 : 1;
}
