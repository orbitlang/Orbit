// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_SUPPORT_SLICE_H_
#define ORBIT_ORBITER_DATATYPE_SUPPORT_SLICE_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/obase.h>

namespace orbiter::datatype::support {
    /**
     * @brief Validated slice bounds, decoupled from any sequence length.
     *
     * Produced by ResolveSliceBounds() once type-checking and the `step != 0`
     * invariant have been verified. Combine with a length via NormalizeSlice()
     * to obtain a concrete iteration plan.
     *
     * `*_omitted` flags carry the "user didn't pass this bound" information: when
     * true the corresponding raw value is meaningless and a direction-dependent
     * default kicks in during normalisation.
     */
    struct SliceArgs {
        IntegerUnderlying start;
        IntegerUnderlying stop;
        IntegerUnderlying step;

        bool start_omitted;
        bool stop_omitted;
        bool step_omitted;
    };

    /**
     * @brief Concrete iteration plan for `[start:stop:step]` over a sequence of length L.
     *
     * The fields describe an iteration that any sequence type can consume uniformly:
     * @code
     *   for (MSize n = 0; n < count; n++) { use(seq[start]); start += step; }
     * @endcode
     *
     * `count` is the exact number of elements the slice yields (zero for empty
     * slices, including reversed-direction slices). `step` is never zero.
     */
    struct SliceIndices {
        /// First index to read.
        MSSize start;

        /// Stride between consecutive selected indices. Always non-zero.
        MSSize step;

        /// Number of elements selected. Bounded above by the sequence length.
        MSize count;
    };

    /**
     * @brief Extract and type-check slice bounds from runtime objects.
     *
     * Each of `start`/`stop`/`step` may be nil (treated as omitted) or a Number;
     * any other type raises a TypeError on the isolate. A non-nil `step` of zero
     * raises a ValueError. On error the function returns false with the panic
     * state set and @p out left untouched.
     *
     * This step is independent of the target sequence length — pair it with
     * NormalizeSlice() once the length is known (typically after acquiring the
     * sequence's lock) to obtain a concrete iteration plan. Splitting the two
     * phases lets the caller perform error reporting outside the critical
     * section.
     *
     * @param isolate  Isolate used for error reporting.
     * @param start    `start` bound — nil for omitted.
     * @param stop     `stop` bound  — nil for omitted.
     * @param step     `step` bound  — nil for omitted, must be non-zero otherwise.
     * @param out      Validated bounds (only written on success).
     *
     * @return true on success, false if a TypeError or ValueError was raised.
     */
    bool ResolveSliceBounds(Isolate *isolate, const OObject *start, const OObject *stop, const OObject *step,
                            SliceArgs &out);

    /**
     * @brief Normalise validated slice bounds against a sequence length (pure math).
     *
     * Follows Python `[start:stop:step]` semantics:
     *  - Any of start/stop/step omitted via `*_omitted` flags falls back to the
     *    direction-dependent default (forward: 0..length, backward: length-1..-1;
     *    step defaults to 1).
     *  - Negative indices wrap once against @p length.
     *  - Values that would land outside the iteration window are clamped to the
     *    slice-relevant edge.
     *
     * The caller must have already validated `args.step != 0` when not omitted
     * (ResolveSliceBounds() guarantees this). No allocations, no error paths.
     *
     * @param length  Length of the target sequence (must be non-negative).
     * @param args    Validated slice bounds, typically returned by ResolveSliceBounds().
     *
     * @return Iteration plan describing the slice over a sequence of @p length items.
     */
    SliceIndices NormalizeSlice(MSSize length, const SliceArgs &args) noexcept;
}

#endif // !ORBIT_ORBITER_DATATYPE_SUPPORT_SLICE_H_
