// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/config.h>

using namespace orbiter;

constexpr Config DefaultConfig = {
    .ost_max = -1,
    .vc_max = -1,
    .fiber_ssize = -1,
    .fiber_pool = -1
};

const Config *orbiter::kConfigDefault = &DefaultConfig;
