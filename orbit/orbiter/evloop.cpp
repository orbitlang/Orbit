// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/evloop.h>

using namespace orbiter;
using namespace orbiter::datatype;

bool orbiter::EVLDoNothingAddIP(Fiber *fiber) {
    if (fiber->io.status != GYRO_COMPLETED) {
        EVLRaiseError(fiber, fiber->io.status);

        return false;
    }

    fiber->vm.regs.IP.reg += sizeof(MachineWord);

    return true;
}

gyro_cb_status_t orbiter::ResumeFromEventLoop(gyro_handle_t *, const int status, const size_t transferred,
                                              void *data) noexcept {
    auto *fiber = (Fiber *) data;

    fiber->io.status = status;
    fiber->io.transferred = transferred;

    Orbiter::GetInstance()->PushFiber(fiber);

    return GYRO_CB_SUCCESS;
}

void orbiter::EVLRaiseError(Fiber *fiber, const int status) {
    ErrorSet(fiber->isolate,
             OSError::Details[OSError::ID],
             (OObject *) O_TO_SMI((MSSize) status),
             OSError::Details[OSError::IO_LOOP],
             gyro_strerror(status));
}
