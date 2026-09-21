// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <chrono>
#include <thread>

#include <gyro/gyro.h>

#include <orbit/orbiter/evloop.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/module/modules.h>

using namespace orbiter::datatype;
using namespace orbiter::module;

// Sleeps at or below this length are served by blocking the mutator thread:
// the round trip through the event loop costs more than the sleep itself.
constexpr IntegerUnderlying kSleepInlineThresholdMs = 10;

static IntegerUnderlying MonotonicMs() noexcept {
    using namespace std::chrono;

    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool SleepResume(orbiter::Fiber *fiber) {
    if (fiber->io.status != GYRO_COMPLETED) {
        orbiter::EVLRaiseError(fiber, fiber->io.status);

        return false;
    }

    const auto elapsed = MonotonicMs() - (IntegerUnderlying) fiber->io.udata;
    const auto result = IntNew(fiber->isolate, elapsed);
    if (!result)
        return false;

    fiber->SetRRValue(result.get());
    fiber->AddIP();

    return true;
}

// *********************************************************************************************************************
// PRIMITIVES
// *********************************************************************************************************************

RUNTIME_FUNCTION(chrono_monotonic, monotonic,
                 R"DOC(
@brief Read the monotonic clock, in milliseconds.

The clock never goes backwards and is unaffected by changes to the system
time. Its origin is unspecified, so the absolute value carries no meaning:
use it to measure intervals by subtracting two readings.

@return Milliseconds since an arbitrary fixed point in the past.

@example
    let start = chrono.monotonic()
    do_work()
    io.print("took", chrono.monotonic() - start, "ms")
)DOC", 0, nullptr, false, false) {
    auto *isolate = O_GET_ISOLATE(_func);

    return HOObject(IntNew(isolate, MonotonicMs()));
}

RUNTIME_FUNCTION(chrono_sleep, sleep,
                 R"DOC(
@brief Suspend the current fiber for at least `ms` milliseconds.

The fiber is parked on the event loop and its scheduler thread is free to
run other fibers in the meantime. Very short sleeps are served inline
without going through the loop.

The sleep is never shorter than requested; it may be longer by the loop's
timer granularity and by the time it takes for the fiber to get a scheduler
slot again.

@param ms Milliseconds to sleep, non-negative.

@return The time actually spent asleep, in milliseconds.

@panic OSError If the event loop could not arm or complete the timer.
@panic ValueError If `ms` is negative.

@example
    let slept = chrono.sleep(250)
    io.print("asked 250, slept", slept)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("ms", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying ms;

    if (!NumberExtract(argv[0], ms))
        return {};

    if (ms < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "sleep duration must be non-negative, got %lld",
                 (long long) ms);

        return {};
    }

    const auto start = MonotonicMs();

    if (ms <= kSleepInlineThresholdMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));

        return HOObject(IntNew(isolate, MonotonicMs() - start));
    }

    auto *orbiter = orbiter::Orbiter::GetInstance();
    auto *fiber = orbiter::Fiber::Current();

    fiber->PrepareForEventLoop(SleepResume);
    fiber->io.udata = start;

    const auto rc = gyro_timer_start(orbiter->GetEventLoop(),
                                     ms,
                                     orbiter::ResumeFromEventLoop,
                                     fiber,
                                     nullptr);
    if (rc < 0) {
        fiber->AbortEventLoop();

        orbiter::EVLRaiseError(fiber, rc);

        return {};
    }

    return HOObject(kOddBallNIL);
}

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

constexpr ModuleEntry chrono_entries[] = {
    ORBIT_MODULE_EXPORT_FUNCTION(chrono_monotonic),
    ORBIT_MODULE_EXPORT_FUNCTION(chrono_sleep),

    ORBIT_MODULE_SENTINEL
};

ModuleInit ModuleChrono = {
    "::orbit::chrono",
    "@brief Time and sleeping."
    "\n\n"
    "Provides a monotonic clock for measuring intervals (monotonic) and a "
    "sleep that parks the current fiber on the event loop instead of blocking "
    "its scheduler thread, reporting the time actually elapsed (sleep).",
    "1.0.0",
    chrono_entries,
    nullptr,
    nullptr
};

const ModuleInit *orbiter::module::module_chrono_ = &ModuleChrono;
