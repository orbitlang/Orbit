// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_PANIC_H_
#define ORBIT_ORBITER_PANIC_H_

#include <orbit/orbiter/datatype/obase.h>

namespace orbiter {
    struct Panic {
        Panic *prev;

        datatype::OObject *error;

        PtrSize frame;

        [[nodiscard]] bool ISAborted() const {
            return this->prev != nullptr;
        }
    };

    class PanicContainer {
    public:
        Panic **r_current_ = nullptr;
        Panic *current_ = nullptr;

        Panic *CreatePanic(Isolate *isolate, Panic **panic_cache, datatype::OObject *error) const noexcept;

        Panic *CreateOOMPanic(const Isolate *isolate, Panic **panic_cache) const;

        void DiscardPanic(Panic **panic_cache) const noexcept;

        void Reset() noexcept;

        /**
         * @brief True if a panic is currently held by this container.
         */
        [[nodiscard]] bool HasPanic() const noexcept {
            return *this->r_current_ != nullptr;
        }
    };

    /**
     * @brief Render the panic chain held by @p container into @p out.
     *
     * Walks the chain from newest (the panic that is currently in flight)
     * to oldest (the one that was being unwound when the newer ones were
     * raised), separating successive entries with a `"\n  while handling:"`
     * marker so the trace reads top-down like a typical exception cause
     * chain.
     *
     * Behavior matches `snprintf`: writes at most `out_size - 1` characters
     * and always terminates with NUL when @p out_size > 0; returns the
     * number of characters that would have been written had the buffer
     * been unbounded (values `>= out_size` signal truncation).
     *
     * Safe to call when there is no active panic — writes an empty string
     * and returns 0.  Each Error in the chain is formatted via
     * `datatype::ErrorFormat`.
     *
     * @param container Source panic container (e.g. `&isolate->panic` or
     *                  `&fiber->panic`).  `nullptr` is tolerated.
     * @param out       Destination buffer.
     * @param out_size  Size of @p out in bytes, including space for the NUL.
     *
     * @return Characters that would have been written (excluding the NUL).
     */
    int PanicFormat(const PanicContainer *container, char *out, size_t out_size) noexcept;
} // namespace orbiter

#endif // !ORBIT_ORBITER_PANIC_H_
