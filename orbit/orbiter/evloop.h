// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_EVLOOP_H_
#define ORBIT_ORBITER_EVLOOP_H_

#include <gyro/gyro.h>

namespace orbiter {
    class Fiber;

    /**
     * @brief Generic on_resume for operations with no result to publish.
     *
     * On GYRO_COMPLETED it advances IP past the CALL and leaves RR as the native
     * left it. On any other status it raises the corresponding OSError.
     */
    bool EVLDoNothingAddIP(Fiber *fiber);

    /**
     * @brief on_resume for operations that fill a Bytes buffer (e.g. a read).
     *
     * On GYRO_COMPLETED or GYRO_EOF it sets the length of the Bytes held in `io.object`
     * to `io.transferred` (0 at end of stream), publishes that same object in RR as the
     * return value of the native, and advances IP past the CALL. On any other status, it
     * raises the corresponding OSError and leaves the object untouched.
     *
     * The native must pass the Bytes as `io_object` to PrepareForEventLoop, and point
     * `io.buf` at its buffer, so that `io.transferred` never exceeds its capacity.
     *
     * @param fiber The fiber being resumed; `io.object` must be a Bytes.
     *
     * @return True if the result was published, false if an error was raised.
     */
    bool EVLReturnFilledBytes(Fiber *fiber);

    /**
     * @brief on_resume for operations that write into a caller-owned, pinned Bytes (e.g. readinto).
     *
     * First releases the pin on the SharedBuffer of the Bytes held in `io.object`,
     * whatever the outcome. Then, on GYRO_COMPLETED or GYRO_EOF, it publishes
     * `io.transferred` in RR as an Int (0 at end of stream) and advances IP past the
     * CALL; on any other status it raises the corresponding OSError. The length of
     * the Bytes is never changed.
     *
     * The native must pass the Bytes as `io_object` to PrepareForEventLoop and pin
     * its buffer (BytesWriteGuard::PinBuffer) only when the operation is left
     * pending, so that this callback's Unpin is balanced.
     *
     * @param fiber The fiber being resumed; `io.object` must be a pinned Bytes.
     *
     * @return True if the result was published, false if an error was raised.
     */
    bool EVLReturnTransferred(Fiber *fiber);

    /**
     * @brief on_resume for operations that send a caller-owned buffer (e.g. a write).
     *
     * First releases the pin on the SharedBuffer of the Bytes held in `io.object`,
     * whatever the outcome; a String holds no pin, so nothing is released for one.
     * Then, on GYRO_COMPLETED, it publishes `io.transferred` in RR as an Int and
     * advances IP past the CALL; on any other status it raises the corresponding
     * OSError. A write has no end of stream: GYRO_EOF is not one of its outcomes.
     *
     * The native must pass the source as `io_object` to PrepareForEventLoop and pin
     * it (ByteView::PinBuffer) only when the operation is left pending, so that this
     * callback's Unpin is balanced.
     *
     * @param fiber The fiber being resumed; `io.object` must be the Bytes or String
     *              that was sent.
     *
     * @return True if the result was published, false if an error was raised.
     */
    bool EVLReturnWritten(Fiber *fiber);

    /**
     * @brief Completion callback shared by every operation submitted with a fiber as user data.
     *
     * Runs on the loop thread: it only records the outcome in FiberIO and hands the
     * fiber back to the scheduler. Interpreting the outcome (return value, panic) is
     * the job of the fiber's on_resume, which runs on the mutator with the GC region active.
     */
    gyro_cb_status_t ResumeFromEventLoop(gyro_handle_t *handle, int status, size_t transferred, void *data) noexcept;

    /**
     * @brief Raises an OSError(IO_LOOP) on the fiber for a failed gyro status.
     *
     * Meant to be called from an on_resume callback (mutator thread, fiber current)
     * or from a native right after a synchronous submit failure. The raw gyro code
     * is attached as the error `details` (SMI), mirroring what ErrorSetFromErrno
     * does with errno, so Orbit code can dispatch on it.
     */
    void EVLRaiseError(Fiber *fiber, int status);
} // namespace orbiter

#endif // !ORBIT_ORBITER_EVLOOP_H_
