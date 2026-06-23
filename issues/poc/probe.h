// Shared helper for issues/poc/*.cpp probes.
//
// Building an Isolate requires a Config and a one-time runtime init, mirroring
// the startup sequence in orbiter/orbit.cpp. Centralized here so probes stay
// small and a future API change only needs editing in one place.
#ifndef ORBIT_VULN_POC_PROBE_H_
#define ORBIT_VULN_POC_PROBE_H_

#include <orbit/orbiter/config.h>
#include <orbit/orbiter/isolate.h>
#include <orbit/orbiter/runtime.h>
#include <orbit/orbiter/memory/memory.h>

namespace probe {
    inline orbiter::Isolate *isolate() {
        static orbiter::Config config{};
        orbiter::memory::MemoryCopy(&config, orbiter::kConfigDefault, sizeof(orbiter::Config));
        orbiter::Orbiter::Initialize(&config);
        return orbiter::Isolate::New(&config);
    }
}

#endif // !ORBIT_VULN_POC_PROBE_H_
