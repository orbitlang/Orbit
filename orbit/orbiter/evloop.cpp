// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/bytes.h>
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

    fiber->AddIP();

    return true;
}

bool orbiter::EVLReturnFilledBytes(Fiber *fiber) {
    if (fiber->io.status != GYRO_COMPLETED && fiber->io.status != GYRO_EOF) {
        EVLRaiseError(fiber, fiber->io.status);

        return false;
    }

    assert(O_IS_TYPE(fiber->io.object.get(), InstanceType::BYTES));

    ((Bytes *) fiber->io.object.get())->length = fiber->io.transferred;

    fiber->SetRRValue(fiber->io.object.get());
    fiber->AddIP();

    return true;
}

bool orbiter::EVLReturnTransferred(Fiber *fiber) {
    const auto buffer = (Bytes *) fiber->io.object.get();

    assert(O_IS_TYPE(buffer, InstanceType::BYTES));

    buffer->shared->Unpin();

    if (fiber->io.status != GYRO_COMPLETED && fiber->io.status != GYRO_EOF) {
        EVLRaiseError(fiber, fiber->io.status);

        return false;
    }

    fiber->SetRRValue(O_TO_SMI(fiber->io.transferred));
    fiber->AddIP();

    return true;
}

bool orbiter::EVLReturnWritten(Fiber *fiber) {
    auto *source = fiber->io.object.get();

    // Only a Bytes was pinned: a String cannot move, so there was nothing to
    // hold in place and there is nothing to release.
    if (O_IS_TYPE(source, InstanceType::BYTES))
        ((Bytes *) source)->shared->Unpin();

    if (fiber->io.status != GYRO_COMPLETED) {
        EVLRaiseError(fiber, fiber->io.status);

        return false;
    }

    fiber->SetRRValue(O_TO_SMI(fiber->io.transferred));
    fiber->AddIP();

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
