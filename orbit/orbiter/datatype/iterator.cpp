// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/iterator.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

void IteratorTrace(const Iterator *self, const GCTraceCallback callback, const MSize epoch) {
    callback((OObject *) self->source, epoch);
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Iterators are unique handles to a stateful walk — identity equality only.
static bool IteratorEqual(const OObject *left, const OObject *right, bool &out) {
    out = left == right;

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — ITERATION
// *********************************************************************************************************************

/// `iter(iter(x))` is `iter(x)` — an iterator returns itself.
static OObject *IteratorGetIter(OObject *self) {
    return self;
}

static CallResult IteratorIterNext(Iterator *self, OObject **out) {
    self->last_result = self->step(self, out);

    return self->last_result;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

static bool IteratorToBool(const Iterator *self) {
    return self->last_result == CallResult::CONTINUE;
}

/// Renders as `iterator <SourceType> at 0xADDR`, prefixed with `reverse `
/// when the iterator was produced via `get_riter`.
static OObject *IteratorToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *it = (const Iterator *) self;

    char src_type[24];
    GetTypeName(isolate, it->source, src_type, sizeof(src_type));

    const auto s = ORStringFormat(isolate, "%siterator <%s> at %p",
                                  it->reverse ? "reverse " : "", src_type, self);

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::IteratorTypeSetup(TypeInfo *self) {
    self->trace = (TraceFn) IteratorTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = IteratorEqual;
    ops.get_iter = IteratorGetIter;
    ops.iter_next = (IterNextFn) IteratorIterNext;
    ops.to_bool = (ToBoolFn) IteratorToBool;
    ops.to_string = IteratorToString;

    return true;
}

HIterator orbiter::datatype::IteratorNew(Isolate *isolate, OObject *source, const IteratorStepFn step) {
    auto *iter = MakeObject<Iterator>(isolate, InstanceType::ITERATOR);
    if (iter != nullptr) {
        iter->source = source;
        iter->step = step;

        iter->snapshot_length = 0;
        iter->state.index = 0;
        iter->last_result = CallResult::CONTINUE;
        iter->reverse = false;
    }

    O_GC_TRACK_RETURN(isolate, iter, true);
}

HOType orbiter::datatype::IteratorTypeInit(Isolate *isolate) {
    return MakeType(isolate, "Iterator", InstanceType::ITERATOR, sizeof(Iterator) - sizeof(OObject), 0, 0);
}
