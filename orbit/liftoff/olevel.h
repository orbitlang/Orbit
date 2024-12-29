// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_OLEVEL_H_
#define ORBIT_LIFTOFF_OLEVEL_H_

namespace liftoff {
    enum class OptimizationLevel {
        OFF,

        SOFT,
        MEDIUM,
        HARD
    };

    constexpr auto kDefaultOptimization = OptimizationLevel::HARD;
    constexpr auto kREPLOptimization = OptimizationLevel::OFF;
}

#endif // !ORBIT_LIFTOFF_OLEVEL_H_
