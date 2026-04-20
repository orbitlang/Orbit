// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/datatype/list.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool ListCheckSize(List *list, const MSize count) {
    if (list->length + count < list->capacity)
        return true;

    MSize len = kListInitialCapacity;

    if (list->objects != nullptr) {
        const MSize grown = (list->capacity + 1) + ((list->capacity + 1) / 2); // 1.5x

        len = grown > list->capacity + count ? grown : list->capacity + count;
    }

    orbiter::memory::IsolateAllocator allocator(O_GET_TYPE(list)->isolate);

    auto **tmp = allocator.realloc(list->objects, len * sizeof(void *));
    if (tmp == nullptr)
        return false;

    list->objects = tmp;
    list->capacity = len;

    return true;
}

bool ListDtor(const List *self) {
    const orbiter::memory::IsolateAllocator allocator(O_GET_TYPE(self)->isolate);

    allocator.free(self->objects);

    self->lock.~AsyncRWLock();

    return true;
}

void ListTrace(const List *self, GCTraceCallback callback, const MSize epoch) {
    for (auto i = 0; i < self->length; i++) {
        const auto obj = self->objects[i];

        if (O_IS_OBJECT(obj))
            callback(obj, epoch);
    }
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Structural equality: same length and element-wise Equal.
static bool ListEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(left) || !O_IS_OBJECT(right))
        return false;

    if (!O_IS_TYPE(left, InstanceType::LIST) || !O_IS_TYPE(right, InstanceType::LIST))
        return false;

    auto *l = (List *) left;
    auto *r = (List *) right;

    std::shared_lock ll(l->lock);
    std::shared_lock lr(r->lock);

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

/// Concatenation: [a, b] + [c, d] → [a, b, c, d].
static bool ListAdd(const OObject *left, const OObject *right, OObject *&result) {
    if (!O_IS_OBJECT(right))
        return false;

    if (!O_IS_TYPE(right, InstanceType::LIST) && !O_IS_TYPE(right, InstanceType::TUPLE))
        return false;

    HList new_list;
    MSize total = 0;

    auto *l = (List *) left;

    std::shared_lock ll(l->lock);

    if (O_IS_TYPE(right, InstanceType::TUPLE)) {
        const auto *r = (Tuple *) right;

        total = l->length + r->length;
        new_list = ListNew(O_GET_ISOLATE(left), total > 0 ? total : kListInitialCapacity);
        if (!new_list)
            return false;

        for (MSize i = 0; i < l->length; i++)
            new_list->objects[i] = l->objects[i];

        for (MSize i = 0; i < r->length; i++)
            new_list->objects[l->length + i] = r->objects[i];
    } else {
        auto *r = (List *) right;

        std::shared_lock lr(r->lock, std::defer_lock);

        if (l != r)
            lr.lock();

        total = l->length + r->length;

        new_list = ListNew(O_GET_ISOLATE(left), total > 0 ? total : kListInitialCapacity);
        if (!new_list)
            return false;

        for (MSize i = 0; i < l->length; i++)
            new_list->objects[i] = l->objects[i];

        for (MSize i = 0; i < r->length; i++)
            new_list->objects[l->length + i] = r->objects[i];
    }

    new_list->length = total;

    result = (OObject *) new_list.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-empty list is truthy.
static bool ListToBool(const OObject *self) {
    return ((const List *) self)->length != 0;
}

static OObject *ListToString(orbiter::Isolate *isolate, const OObject *self) {
    assert(false);
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(list_append, append,
               R"DOC(
@brief Append an object to the end of the list.

@param object  The value to append.

@see prepend, insert, extend

@example
    let l = []
    l.append(1)
    l.append(2)
    l.length()    // 2
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    if (!ListAppend(self, argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(list_clear, clear,
               R"DOC(
@brief Remove all elements from the list in-place.

The backing buffer is retained for reuse; only the length is reset to zero.

@see copy, length

@example
    let l = [1, 2, 3]
    l.clear()
    l.length()    // 0
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    std::unique_lock _(self->lock);

    self->length = 0;

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(list_contains, contains,
               R"DOC(
@brief Return true if the list contains the given value.

Uses structural equality (==) for comparison.

@param value  The value to search for.

@return true if found, false otherwise.

@see count, index

@example
    [1, 2, 3].contains(2)    // true
    [1, 2, 3].contains(9)    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    std::shared_lock _(self->lock);

    for (MSize i = 0; i < self->length; i++) {
        if (Equal(self->objects[i], argv[1]))
            return HOObject((OObject *) BOOL_TO_OBOOL(true));
    }

    return HOObject((OObject *) BOOL_TO_OBOOL(false));
}

RUNTIME_METHOD(list_count, count,
               R"DOC(
@brief Return the number of times a value appears in the list.

Uses structural equality (==) for comparison.

@param value  The value to count.

@return A non-negative Int.

@see contains, index

@example
    [1, 2, 2, 3].count(2)    // 2
    [1, 2, 3].count(9)        // 0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    std::shared_lock _(self->lock);

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

RUNTIME_METHOD(list_extend, extend,
               R"DOC(
@brief Extend the list by appending all elements from another List or Tuple.

@param other  The source List or Tuple whose elements are appended.

@panic TypeError   When `other` is neither a List nor a Tuple.

@see append, copy

@example
    let l = [1, 2]
    l.extend([3, 4])
    l.length()    // 4
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::LIST),
                   PCHECK_DEF("other", false, InstanceType::LIST, InstanceType::TUPLE)
    );
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    if (!ListExtend(self, argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(list_get, get,
               R"DOC(
@brief Return the element at the given index.

Supports negative indices: -1 refers to the last element.

@param index  Integer position. Must be in [-length, length).

@return The element at that position.

@panic ValueError  When `index` is out of range.
@panic TypeError   When `index` is not an integer.

@see length, insert, remove

@example
    [10, 20, 30].get(0)     // 10
    [10, 20, 30].get(-1)    // 30
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::LIST),
                   PCHECK_DEF("index", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying index;
    NumberExtract(argv[1], index);

    bool success;
    auto result = ListGet(self, &success, index);
    if (!success) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "list index %lld out of range [0, %lld)",
                 (long long) index, (long long) self->length);

        return {};
    }

    return result;
}

RUNTIME_METHOD(list_index, index,
               R"DOC(
@brief Return the index of the first occurrence of a value.

Uses structural equality (==) for comparison.

@param value  The value to search for.

@return The Int index of the first match, -1 otherwise.

@see contains, count

@example
    [10, 20, 30].index(20)    // 1
    [10, 20, 30].index(99)    // -1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    std::shared_lock _(self->lock);

    for (MSize i = 0; i < self->length; i++) {
        if (Equal(self->objects[i], argv[1])) {
            auto n = IntNew(isolate, (IntegerUnderlying) i);
            if (!n)
                return {};

            return HOObject(std::move(n));
        }
    }

    return HOObject((OObject *) O_TO_SMI(-1));
}

RUNTIME_METHOD(list_insert, insert,
               R"DOC(
@brief Insert an object at the given index.

Supports negative indices: -1 inserts before the last element.
If `index` is beyond the end of the list the element is appended.

@param index   Integer position.
@param object  The value to insert.

@panic TypeError   When `index` is not an integer.

@see append, prepend, remove

@example
    let l = [1, 3]
    l.insert(1, "hello")
    l.get(1)    // "hello"
)DOC", 3, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::LIST),
                   PCHECK_DEF("index", false, InstanceType::NUMBER)
    );
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    IntegerUnderlying index;
    NumberExtract(argv[1], index);

    if (!ListInsert(self, argv[2], index))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(list_length, length,
               R"DOC(
@brief Return the number of elements in the list.

@return A non-negative Int.

@see get, clear

@example
    [1, 2, 3].length()    // 3
    [].length()            // 0
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    const auto *self = (const List *) argv[0];

    auto n = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) self->length);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(list_prepend, prepend,
               R"DOC(
@brief Insert an object at the beginning of the list.

@param object  The value to prepend.

@see append, insert

@example
    let l = [2, 3]
    l.prepend(1)
    l.get(0)    // 1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::LIST));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    if (!ListPrepend(self, argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(list_remove, remove,
               R"DOC(
@brief Remove the element at the given index.

Supports negative indices: -1 removes the last element.
If the index is out of range the list is left unchanged.

@param index  Integer position.

@panic TypeError  When `index` is not an integer.

@see insert, clear

@example
    let l = [1, 2, 3]
    l.remove(1)
    l.length()    // 2
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::LIST),
                   PCHECK_DEF("index", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *self = (List *) argv[0];

    IntegerUnderlying index;
    NumberExtract(argv[1], index);

    ListRemove(self, index);

    return HOObject(kOddBallNIL);
}

constexpr FunctionDef list_methods[] = {
    list_append,
    list_clear,
    list_contains,
    list_count,
    list_extend,
    list_get,
    list_index,
    list_insert,
    list_length,
    list_prepend,
    list_remove,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ListAppend(List *list, OObject *object) {
    std::unique_lock _(list->lock);

    if (!ListCheckSize(list, 1))
        return false;

    list->objects[list->length++] = object;

    return true;
}

bool orbiter::datatype::ListAppend(List *list, List *other) {
    std::unique_lock _(list->lock);

    if (other == nullptr)
        return true;

    std::shared_lock other_lock(other->lock, std::defer_lock);

    if (list != other)
        other_lock.lock();

    if (!ListCheckSize(list, other->length))
        return false;

    const auto src_length = other->length;
    for (MSize i = 0; i < src_length; i++)
        list->objects[list->length++] = other->objects[i];

    return true;
}

bool orbiter::datatype::ListExtend(List *list, OObject *other) {
    if (O_IS_TYPE(other, InstanceType::LIST))
        return ListAppend(list, (List *) other);

    if (O_IS_TYPE(other, InstanceType::TUPLE)) {
        std::unique_lock _(list->lock);

        const auto count = ((Tuple *) other)->length;
        auto **objects = ((Tuple *) other)->objects;

        if (!ListCheckSize(list, count))
            return false;

        for (auto i = 0; i < count; i++)
            list->objects[list->length + i] = objects[i];

        list->length += count;

        return true;
    }

    assert(false);
}

bool orbiter::datatype::ListInsert(List *list, OObject *object, MSSize index) {
    std::unique_lock _(list->lock);

    if (list->length == 0 || index >= (MSSize) list->length) {
        if (!ListCheckSize(list, 1))
            return false;

        list->objects[list->length++] = object;

        return true;
    }

    index = ((index % (MSSize) list->length) + (MSSize) list->length) % (MSSize) list->length;

    list->objects[index] = object;

    return true;
}

bool orbiter::datatype::ListPrepend(List *list, OObject *object) {
    std::unique_lock _(list->lock);

    if (!ListCheckSize(list, 1))
        return false;

    for (MSize i = list->length; i > 0; i--)
        list->objects[i] = list->objects[i - 1];

    list->objects[0] = object;

    list->length++;

    return true;
}

bool orbiter::datatype::ListTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) ListDtor;
    self->trace = (TraceFn) ListTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = ListEqual;
    ops.add = ListAdd;
    ops.to_bool = ListToBool;
    ops.to_string = ListToString;
    ops.to_repr = ListToString;

    return TIPropertyAdd(self, list_methods, PropertyFlag::IS_PUBLIC);
}

HList orbiter::datatype::ListNew(Isolate *isolate, const MSize capacity) {
    auto *list = MakeObject<List>(isolate, InstanceType::LIST);
    if (list == nullptr)
        return {};

    list->objects = nullptr;
    list->capacity = capacity;
    list->length = 0;

    if (capacity > 0) {
        memory::IsolateAllocator allocator(isolate);

        list->objects = allocator.alloc<OObject *>(capacity * sizeof(void *));
        if (list->objects == nullptr) {
            isolate->gc->RawFree((OObject *) list, false);

            return {};
        }
    }

    new(&list->lock)sync::AsyncRWLock();

    O_GC_TRACK_RETURN(isolate, list, true);
}

HOObject orbiter::datatype::ListGet(List *list, bool *success, MSSize index) {
    std::shared_lock _(list->lock);

    *success = false;

    if (list->length == 0 || index >= (MSSize) list->length)
        return {};

    index = ((index % (MSSize) list->length) + (MSSize) list->length) % (MSSize) list->length;

    *success = true;

    return HOObject(list->objects[index]);
}

HOType orbiter::datatype::ListTypeInit(Isolate *isolate) {
    auto list = MakeType(isolate, "List", InstanceType::LIST, sizeof(List) - sizeof(OObject), 11, 0);
    return list;
}

void orbiter::datatype::ListRemove(List *list, MSSize index) {
    std::unique_lock _(list->lock);

    if (list->length == 0 || index >= (MSSize) list->length)
        return;

    index = ((index % (MSSize) list->length) + (MSSize) list->length) % (MSSize) list->length;

    // Move items back
    for (auto i = index + 1; i < list->length; i++)
        list->objects[i - 1] = list->objects[i];

    list->length--;
}
