// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_CONFIG_H_
#define ORBIT_ORBITER_CONFIG_H_

#include <orbit/datatype.h>

namespace orbiter {
    constexpr auto kEvarPath = "ORBIT_PATH";
    constexpr auto kEvarStartup = "ORBIT_STARTUP";
    constexpr auto kEvarMaxVC = "ORBIT_MAXVC";

    struct Config {
        char **argv;

        I32 argc;
        I32 file;
        I32 cmd;

        I32 ost_max;
        I32 vc_max;
        I32 fiber_ssize;
        I32 fiber_pool;

        bool interactive;
        bool quiet;
    };

    extern const Config *kConfigDefault;

    bool ConfigInit(Config *config, int argc, char **argv);

} // namespace orbiter

#endif // !ORBIT_ORBITER_CONFIG_H_
