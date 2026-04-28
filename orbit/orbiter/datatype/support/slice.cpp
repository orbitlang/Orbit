// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/number.h>

#include <orbit/orbiter/datatype/support/slice.h>

using namespace orbiter::datatype;

bool extract(orbiter::Isolate *isolate, const OObject *bound, IntegerUnderlying &raw, bool &omitted) {
    omitted = false;

    if (O_IS_NIL(bound)) {
        omitted = true;
        raw = 0;

        return true;
    }

    if (!NumberExtract(bound, raw)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            isolate->primitive[(int) InstanceType::NUMBER]->name,
                            bound);

        return false;
    }

    return true;
}

bool support::ResolveSliceBounds(Isolate *isolate, const OObject *start, const OObject *stop, const OObject *step,
                                 SliceArgs &out) {
    if (!extract(isolate, step, out.step, out.step_omitted))
        return false;

    if (!out.step_omitted && out.step == 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "slice step cannot be zero");

        return false;
    }

    if (!extract(isolate, start, out.start, out.start_omitted))
        return false;

    if (!extract(isolate, stop, out.stop, out.stop_omitted))
        return false;

    return true;
}

support::SliceIndices support::NormalizeSlice(const MSSize length, const SliceArgs &args) noexcept {
    const MSSize step = args.step_omitted ? 1 : (MSSize) args.step;

    // Resolve start: either pick the side-dependent default, wrap a single
    // negative offset, or clamp to the slice-relevant edge.
    MSSize start;
    if (args.start_omitted)
        start = (step < 0) ? length - 1 : 0;
    else if (args.start < 0) {
        const auto wrapped = args.start + length;
        start = wrapped < 0 ? ((step < 0) ? -1 : 0) : (MSSize) wrapped;
    } else if (args.start >= length)
        start = (step < 0) ? length - 1 : length;
    else
        start = (MSSize) args.start;

    // Resolve stop using the same rules.
    MSSize stop;
    if (args.stop_omitted)
        stop = (step < 0) ? -1 : length;
    else if (args.stop < 0) {
        const auto wrapped = args.stop + length;
        stop = wrapped < 0 ? ((step < 0) ? -1 : 0) : (MSSize) wrapped;
    } else if (args.stop >= length)
        stop = (step < 0) ? length - 1 : length;
    else
        stop = (MSSize) args.stop;

    // Output length: ceil((stop - start) / step) when the slice runs in the
    // step's direction, otherwise zero.
    MSize count = 0;
    if (step > 0 && stop > start)
        count = (MSize) ((stop - start - 1) / step + 1);
    else if (step < 0 && stop < start)
        count = (MSize) ((start - stop - 1) / (-step) + 1);

    return {start, step, count};
}
