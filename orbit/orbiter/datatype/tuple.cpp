// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/datatype/tuple.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool TupleDtor(const Tuple *self) {
    const orbiter::memory::IsolateAllocator allocator(O_GET_ISOLATE(self));

    allocator.free(self->objects);

    return true;
}

void TupleTrace(const Tuple *self, const GCTraceCallback callback, const MSize epoch) {
    for (int i = 0; i < self->length; i++) {
        const auto obj = self->objects[i];

        if (O_IS_OBJECT(obj))
            callback(obj, epoch);
    }
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Structural equality: same length and element-wise Equal.
static bool TupleEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(left) || !O_IS_OBJECT(right))
        return false;

    if (!O_IS_TYPE(left, InstanceType::TUPLE) || !O_IS_TYPE(right, InstanceType::TUPLE))
        return false;

    const auto *l = (const Tuple *) left;
    const auto *r = (const Tuple *) right;

    if (l->length != r->length)
        return false;

    for (MSize i = 0; i < l->length; i++) {
        if (!Equal(l->objects[i], r->objects[i]))
            return false;
    }

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — ARITHMETIC
// *********************************************************************************************************************

/// Concatenation: (a, b) + (c, d) → (a, b, c, d).
static bool TupleAdd(const OObject *left, const OObject *right, OObject *&result) {
    if (!O_IS_OBJECT(left) || !O_IS_OBJECT(right))
        return false;

    if (!O_IS_TYPE(left, InstanceType::TUPLE) || !O_IS_TYPE(right, InstanceType::TUPLE))
        return false;

    const auto *l = (const Tuple *) left;
    const auto *r = (const Tuple *) right;
    const auto length = l->length + r->length;

    const auto t = TupleNew(O_GET_ISOLATE(left), length);
    if (!t)
        return false;

    for (MSize i = 0; i < l->length; i++)
        t->objects[i] = l->objects[i];

    for (MSize i = 0; i < r->length; i++)
        t->objects[l->length + i] = r->objects[i];

    t->length = length;

    result = (OObject *) t.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — INDEX
// *********************************************************************************************************************

/// `tuple[index]`: integer indexing with negative-wrap semantics. Out-of-range raises
/// IndexError; a non-integer index raises TypeError.
static bool TupleLoadIndex(const OObject *self, const OObject *index, OObject *&result) {
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying i;
    if (!NumberExtract(index, i)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            isolate->primitive[(int) InstanceType::NUMBER]->name,
                            index);

        return false;
    }

    const auto *t = (const Tuple *) self;

    if (t->length == 0 || i >= (IntegerUnderlying) t->length) {
        ErrorSet(isolate,
                 IndexError::Details[IndexError::Reason::ID],
                 nullptr,
                 IndexError::Details[IndexError::Reason::OUT_OF_RANGE],
                 O_GET_TYPE(self)->name, (long long) i, (long long) t->length);

        return false;
    }

    i = ((i % (IntegerUnderlying) t->length) + (IntegerUnderlying) t->length) % (IntegerUnderlying) t->length;

    result = t->objects[i];

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-empty tuple is truthy.
static bool TupleToBool(const OObject *self) {
    return ((const Tuple *) self)->length != 0;
}

static OObject *TupleToString(orbiter::Isolate *isolate, const Tuple *self) {
    constexpr unsigned char open_paren[] = {'('};
    constexpr unsigned char close_paren[] = {')'};
    constexpr unsigned char close_single[] = {',', ')'};
    constexpr unsigned char item_sep[] = {',', ' '};

    StringBuilder builder(isolate);

    // Rough hint: 8 bytes per element for the initial buffer.
    if (!builder.Write(open_paren, 1, self->length * 8 + 2))
        return nullptr;

    for (MSize i = 0; i < self->length; i++) {
        if (i > 0 && !builder.Write(item_sep, 2, 0))
            return nullptr;

        auto v = Repr(isolate, self->objects[i]);
        if (!v)
            return nullptr;

        if (!builder.Write((const ORString *) v.get(), 0))
            return nullptr;
    }

    if (self->length == 1) {
        if (!builder.Write(close_single, 2, 0))
            return nullptr;
    } else {
        if (!builder.Write(close_paren, 1, 0))
            return nullptr;
    }

    return (OObject *) ORStringNew(isolate, builder).get();
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

/// Polynomial hash of all element hashes, cached in self->hash.
static MSize TupleHash(const OObject *self) {
    auto *tuple = const_cast<Tuple *>((const Tuple *) self);

    if (tuple->hash != 0)
        return tuple->hash;

    MSize h = 0;
    for (MSize i = 0; i < tuple->length; i++)
        h = h * 31 + Hash(tuple->objects[i]);

    if (h == 0 || h == HASH_ERROR)
        h = 1;

    tuple->hash = h;

    return tuple->hash;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(tuple_contains, contains,
               R"DOC(
@brief Return true if the tuple contains the given value.

Uses structural equality (==) for comparison.

@param value  The value to search for.

@return true if found, false otherwise.

@see count, index

@example
    (1, 2, 3).contains(2)    // true
    (1, 2, 3).contains(9)    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::TUPLE),
                   PCHECK_DEF("value", false)
    );
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(TupleContains((const Tuple *) argv[0], argv[1]) >= 0));
}

RUNTIME_METHOD(tuple_count, count,
               R"DOC(
@brief Return the number of times a value appears in the tuple.

Uses structural equality (==) for comparison.

@param value  The value to count.

@return A non-negative Int.

@see contains, index

@example
    (1, 2, 2, 3).count(2)    // 2
    (1, 2, 3).count(9)        // 0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::TUPLE),
                   PCHECK_DEF("value", false)
    );
    PCHECK_CHECK(params);

    const auto *self = (const Tuple *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying n = 0;

    for (MSize i = 0; i < self->length; i++) {
        if (Equal(self->objects[i], argv[1]))
            n++;
    }

    auto result = IntNew(isolate, n);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(tuple_index, index,
               R"DOC(
@brief Return the index of the first occurrence of a value.

Uses structural equality (==) for comparison.

@param value  The value to search for.

@return The Int index of the first match.

@panic ValueError  When `value` is not found in the tuple.

@see contains, count

@example
    (10, 20, 30).index(20)    // 1
    (10, 20, 30).index(99)    // panic!
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::TUPLE),
                   PCHECK_DEF("value", false)
    );
    PCHECK_CHECK(params);

    const auto *self = (const Tuple *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto i = TupleContains(self, argv[1]);

    if (i < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "value not found in tuple");

        return {};
    }

    auto n = IntNew(isolate, i);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(tuple_length, length,
               R"DOC(
@brief Return the number of elements in the tuple.

@return A non-negative Int.

@see get

@example
    (1, 2, 3).length()    // 3
    ().length()            // 0
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::TUPLE));
    PCHECK_CHECK(params);

    const auto *self = (const Tuple *) argv[0];

    auto n = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) self->length);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

constexpr FunctionDef tuple_methods[] = {
    tuple_contains,
    tuple_count,
    tuple_index,
    tuple_length,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::TupleAppend(Tuple *tuple, OObject *item) {
    if (tuple->length == tuple->capacity)
        return false;

    tuple->objects[tuple->length] = item;

    tuple->length += 1;

    return true;
}

bool orbiter::datatype::TupleTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) TupleDtor;
    self->trace = (TraceFn) TupleTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = TupleEqual;
    ops.add = TupleAdd;
    ops.load_index = TupleLoadIndex;
    ops.to_bool = TupleToBool;
    ops.to_string = (ToStrFn) TupleToString;
    ops.hash = TupleHash;

    return TIPropertyAdd(self, tuple_methods, PropertyFlag::IS_PUBLIC);
}

HOType orbiter::datatype::TupleTypeInit(Isolate *isolate) {
    auto tuple = MakeType(isolate, "Tuple", InstanceType::TUPLE, sizeof(Tuple) - sizeof(OObject), 4, 0);
    return tuple;
}

HTuple orbiter::datatype::TupleNew(Isolate *isolate, const MSize count) {
    auto *tuple = MakeObject<Tuple>(isolate, InstanceType::TUPLE);

    if (tuple != nullptr) {
        memory::IsolateAllocator allocator(isolate);

        tuple->objects = allocator.alloc<OObject *>(count * sizeof(void *));
        if (tuple->objects == nullptr) {
            isolate->gc->RawFree((OObject *) tuple, false);

            return {};
        }

        tuple->capacity = count;
        tuple->length = 0;
        tuple->hash = 0;
    }

    O_GC_TRACK_RETURN(isolate, tuple, true);
}

HTuple orbiter::datatype::TupleNew(OObject *object) {
    if (O_IS_SMI(object))
        return {};

    auto *isolate = O_GET_ISOLATE(object);

    if (O_IS_TYPE(object, InstanceType::DICT)) {
        auto list = (HList) DictKeys((Dict *) object);
        return TupleNewFromList(list);
    }

    if (O_IS_TYPE(object, InstanceType::TUPLE)) {
        const auto *other = (Tuple *) object;
        auto tuple = TupleNew(isolate, other->length);

        for (auto i = 0; i < other->length; i++)
            tuple->objects[i] = other->objects[i];

        tuple->length = other->length;

        return tuple;
    }

    if (O_IS_TYPE(object, InstanceType::LIST)) {
        assert(false);
    }

    return {};
}

HTuple orbiter::datatype::TupleNewFromList(HList &list) {
    auto *isolate = O_GET_ISOLATE(list);

    auto *tuple = MakeObject<Tuple>(isolate, InstanceType::TUPLE);

    if (tuple != nullptr) {
        tuple->objects = list->objects;
        tuple->capacity = list->capacity;
        tuple->length = list->length;
        tuple->hash = 0;

        list->objects = nullptr;
        list->capacity = 0;
        list->length = 0;

        list.reset();
    }

    O_GC_TRACK_RETURN(isolate, tuple, true);
}

MSSize orbiter::datatype::TupleContains(const Tuple *tuple, const OObject *value) {
    for (MSize i = 0; i < tuple->length; i++) {
        if (Equal(tuple->objects[i], value))
            return (MSSize) i;
    }

    return -1;
}
