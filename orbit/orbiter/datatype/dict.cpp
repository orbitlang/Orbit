// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/rguard.h>
#include <orbit/orbiter/datatype/stringbuilder.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/datatype/dict.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool DictDtor(Dict *self) {
    self->dict.Finalize(nullptr);
    self->dict.~ORHMap();

    self->lock.~AsyncRWLock();

    return true;
}

void DictTrace(const Dict *self, const GCTraceCallback callback, const MSize epoch) {
    for (const auto *cursor = self->dict.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
        callback((OObject *) cursor->key, epoch);

        callback(cursor->value, epoch);
    }
}

/// Insert one entry into `dst` without acquiring dst's lock.
/// Caller must hold a unique_lock on dst->lock before calling.
static bool DictInsertLocked(Dict *dst, OObject *key, OObject *value) {
    ORHEntry *entry;

    dst->dict.Lookup(key, &entry);

    if (entry != nullptr) {
        entry->value = value;

        return true;
    }

    entry = dst->dict.AllocHEntry();
    if (entry == nullptr)
        return false;

    entry->key = key;
    entry->value = value;

    if (!dst->dict.Insert(entry)) {
        dst->dict.FreeHEntry(entry);
        return false;
    }

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Two dicts are equal when they have the same length and every key present in
/// left maps to an equal value in right.  Uses the generic Equal() dispatch so
/// that nested objects are compared by their own TypeOps.equal rules.
static bool DictEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::DICT))
        return false;

    auto *a = (Dict *) left;
    auto *b = (Dict *) right;

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    if (a->dict.length != b->dict.length)
        return false;

    for (const auto *cur = a->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        ORHEntry *entry;

        if (!b->dict.Lookup(cur->key, &entry))
            return false;

        if (!Equal(cur->value, entry->value))
            return false;
    }

    return true;
}

/// Membership test: `key in dict` — checks key presence regardless of value type.
static bool DictOpContains(const OObject *container, const OObject *value, bool &result) {
    result = DictContains((Dict *) container, (OObject *) value);

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-empty dict is truthy; an empty dict is falsy.
/// Reads length without the lock — single word reads are practically safe and
/// consistent with how dict_is_empty/dict_length behave.
static bool DictToBool(const OObject *self) {
    return ((const Dict *) self)->dict.length != 0;
}

/// Empty dict renders as `{}`; a dict that (directly or transitively) references
/// itself renders the self-reference as `{...}`, detected via the fiber-local
/// ReprGuard stack before recursing into a value.
static OObject *DictToString(orbiter::Isolate *isolate, Dict *self) {
    const ReprGuard guard((OObject *) self);

    if (guard.IsError())
        return nullptr;

    if (guard.IsCyclic())
        return (OObject *) ORStringNew(isolate, "{...}", 5).get();

    std::shared_lock _(self->lock);

    StringBuilder builder(isolate);

    constexpr unsigned char open_brace[] = {'{'};
    constexpr unsigned char close_brace[] = {'}'};
    constexpr unsigned char item_sep[] = {',', ' '};
    constexpr unsigned char kv_sep[] = {':', ' '};

    // Rough hint: 16 bytes per entry for the initial buffer.
    if (!builder.Write(open_brace, 1, self->dict.length * 16 + 1))
        return nullptr;

    bool first = true;
    for (const auto *cur = self->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!first && !builder.Write(item_sep, 2, 0))
            return nullptr;

        first = false;

        auto k = Repr(isolate, cur->key);
        if (!k)
            return nullptr;

        if (!builder.Write((const ORString *) k.get(), 0))
            return nullptr;

        if (!builder.Write(kv_sep, 2, 0))
            return nullptr;

        auto v = Repr(isolate, cur->value);
        if (!v)
            return nullptr;

        if (!builder.Write((const ORString *) v.get(), 0))
            return nullptr;
    }

    if (!builder.Write(close_brace, 1, 0))
        return nullptr;

    return (OObject *) ORStringNew(isolate, builder).get();
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(dict_clear, clear,
               R"DOC(
@brief Remove all entries from the dictionary in-place.

@see copy, is_empty

@example
    let d = { a: 1, b: 2 }
    d.clear()
    d.is_empty()    // true
)DOC", 1, nullptr, false, false) {
    auto *self = (Dict *) argv[0];

    std::unique_lock _(self->lock);

    self->dict.Clear(nullptr);

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(dict_copy, copy,
               R"DOC(
@brief Return a shallow copy of the dictionary.

Keys and values are not deep-copied; both the original and the copy
reference the same objects.

@return A new Dict with the same entries as self.

@see merge

@example
    let a = { x: 1 }
    let b = a.copy()
    b.set("y", 2)
    a.has("y")    // false
)DOC", 1, nullptr, false, false) {
    return HOObject(DictNew(argv[0]));
}

RUNTIME_METHOD(dict_delete, delete,
               R"DOC(
@brief Remove the entry for the given key from the dictionary.

@param key  The key to remove.

@return true if the key existed and was removed, false otherwise.

@see has, set

@example
    let d = { a: 1 }
    d.delete("a")    // true
    d.delete("a")    // false (already gone)
)DOC", 2, nullptr, false, false) {
    auto *self = (Dict *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(DictRemove(self, argv[1])));
}

RUNTIME_METHOD(dict_entries, entries,
               R"DOC(
@brief Return all key-value pairs as a list of two-element Tuples.

The order matches the insertion order of the dictionary.

@return A List of Tuple(key, value) pairs.

@see keys, values

@example
    let d = { a: 1, b: 2 }
    d.entries()    // [("a", 1), ("b", 2)]
)DOC", 1, nullptr, false, false) {
    auto *self = (Dict *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    std::shared_lock _(self->lock);

    auto list = ListNew(isolate, self->dict.length);
    if (!list)
        return {};

    for (const auto *cur = self->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        auto entry = TupleNew(isolate, 2);
        if (!entry)
            return {};

        if (!TupleAppend(entry.get(), cur->key) || !TupleAppend(entry.get(), cur->value))
            return {};

        if (!ListAppend(list.get(), (OObject *) entry.get()))
            return {};
    }

    return HOObject(std::move(list));
}

RUNTIME_METHOD(dict_get, get,
               R"DOC(
@brief Look up a key and return its value, or a fallback if absent.

@param key      The key to search for.
@param default  Optional fallback value (default: nil).

@return The stored value if the key exists, otherwise default.

@see has, set

@example
    let d = { x: 10 }
    d.get("x")          // 10
    d.get("y")          // nil
    d.get("y", 0)       // 0
)DOC", 3, nullptr, false, false) {
    auto *self = (Dict *) argv[0];

    HOObject value;
    if (DictLookup(self, argv[1], value))
        return value;

    // argv[2] is nil when the optional default was not supplied.
    return HOObject(argv[2]);
}

RUNTIME_METHOD(dict_has, has,
               R"DOC(
@brief Return true when the dictionary contains the given key.

@param key  The key to test.

@return true or false.

@see get, delete

@example
    let d = { a: 1 }
    d.has("a")    // true
    d.has("b")    // false
)DOC", 2, nullptr, false, false) {
    auto *self = (Dict *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(DictContains(self, argv[1])));
}

RUNTIME_METHOD(dict_is_empty, is_empty,
               R"DOC(
@brief Return true when the dictionary has no entries.

@return true if empty, false otherwise.

@see length, clear

@example
    ({}).is_empty()         // true
    ({ a: 1 }).is_empty()   // false
)DOC", 1, nullptr, false, false) {
    const auto *self = (const Dict *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(self->dict.length == 0));
}

RUNTIME_METHOD(dict_keys, keys,
               R"DOC(
@brief Return all keys as a list in insertion order.

@return A List of keys.

@see values, entries

@example
    { a: 1, b: 2 }.keys()    // ["a", "b"]
)DOC", 1, nullptr, false, false) {
    return DictKeys((Dict *) argv[0]);
}

RUNTIME_METHOD(dict_length, length,
               R"DOC(
@brief Return the number of entries in the dictionary.

@return A non-negative Int.

@see is_empty

@example
    { a: 1, b: 2 }.length()    // 2
    ({}).length()               // 0
)DOC", 1, nullptr, false, false) {
    const auto *self = (const Dict *) argv[0];

    auto n = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) self->dict.length);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(dict_merge, merge,
               R"DOC(
@brief Return a new dictionary combining self and other.

Entries from other take precedence when the same key exists in both.

@param other  The dictionary to merge in.

@return A new Dict equal to self + other.

@panic TypeError  When `other` is not a Dict.

@see update, copy

@example
    { a: 1 }.merge({ b: 2 })        // { a: 1, b: 2 }
    { a: 1 }.merge({ a: 99, b: 2 }) // { a: 99, b: 2 }
)DOC", 2, nullptr, false, false) {
    auto *self = (Dict *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    if (!O_IS_OBJECT(argv[1]) || !O_IS_TYPE(argv[1], InstanceType::DICT)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::PARAMETER],
                            "other",
                            argv[1]);

        return {};
    }

    auto *other = (Dict *) argv[1];

    std::shared_lock self_lock(self->lock);
    std::shared_lock other_lock(other->lock);

    auto result = DictNew(isolate, (U32) (self->dict.length + other->dict.length));
    if (!result)
        return {};

    for (const auto *cur = self->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!DictInsert(result.get(), cur->key, cur->value))
            return {};
    }

    for (const auto *cur = other->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!DictInsert(result.get(), cur->key, cur->value))
            return {};
    }

    return HOObject(std::move(result));
}

RUNTIME_METHOD(dict_set, set,
               R"DOC(
@brief Insert or update a key-value pair.

If `key` already exists its value is replaced; otherwise a new entry is
created.

@param key    The key to insert or update.
@param value  The value to associate with `key`.

@return nil.

@see get, delete

@example
    let d = {}
    d.set("x", 42)
    d.get("x")    // 42
)DOC", 3, nullptr, false, false) {
    auto *self = (Dict *) argv[0];

    if (!DictInsert(self, argv[1], argv[2]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(dict_update, update,
               R"DOC(
@brief Merge entries from other into self in-place.

Existing keys are overwritten with values from other.
No-op when other is self.

@param other  The dictionary whose entries are merged in.

@return nil.

@panic TypeError  When `other` is not a Dict.

@see merge, set

@example
    let d = { a: 1 }
    d.update({ b: 2, a: 99 })
    d.get("a")    // 99
    d.get("b")    // 2
)DOC", 2, nullptr, false, false) {
    auto *self = (Dict *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    if (!O_IS_OBJECT(argv[1]) || !O_IS_TYPE(argv[1], InstanceType::DICT)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::PARAMETER],
                            "other",
                            argv[1]);

        return {};
    }

    auto *other = (Dict *) argv[1];

    if (self == other)
        return HOObject(kOddBallNIL);

    // Hold self's unique_lock and other's shared_lock simultaneously to
    // avoid the overhead of re-acquiring self's lock per entry.
    std::unique_lock self_lock(self->lock);
    std::shared_lock other_lock(other->lock);

    for (const auto *cur = other->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!DictInsertLocked(self, cur->key, cur->value))
            return {};
    }

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(dict_values, values,
               R"DOC(
@brief Return all values as a list in insertion order.

@return A List of values.

@see keys, entries

@example
    { a: 1, b: 2 }.values()    // [1, 2]
)DOC", 1, nullptr, false, false) {
    auto *self = (Dict *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    std::shared_lock _(self->lock);

    auto list = ListNew(isolate, self->dict.length);
    if (!list)
        return {};

    for (const auto *cur = self->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!ListAppend(list.get(), cur->value))
            return {};
    }

    return HOObject(std::move(list));
}

constexpr FunctionDef dict_methods[] = {
    dict_clear,
    dict_copy,
    dict_delete,
    dict_entries,
    dict_get,
    dict_has,
    dict_is_empty,
    dict_keys,
    dict_length,
    dict_merge,
    dict_set,
    dict_update,
    dict_values,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::DictContains(Dict *dict, OObject *key) {
    std::shared_lock _(dict->lock);

    ORHEntry *entry = nullptr;

    dict->dict.Lookup(key, &entry);

    return entry != nullptr;
}

bool orbiter::datatype::DictInsert(Dict *dict, OObject *key, OObject *value) {
    std::unique_lock _(dict->lock);

    return DictInsertLocked(dict, key, value);
}

bool orbiter::datatype::DictInsert(Dict *dict, const char *key, OObject *value) {
    auto okey = ORStringNew(O_GET_ISOLATE(dict), key);

    if (okey)
        return DictInsert(dict, (OObject *) okey.get(), value);

    return false;
}

bool orbiter::datatype::DictLookup(Dict *dict, OObject *key, HOObject &out_value) {
    std::shared_lock _(dict->lock);

    ORHEntry *entry;

    if (dict->dict.Lookup(key, &entry)) {
        out_value = Handle(entry->value);

        return true;
    }

    return false;
}

bool orbiter::datatype::DictLookup(Dict *dict, const char *key, HOObject &out_value) {
    auto okey = ORStringNew(O_GET_ISOLATE(dict), key);

    if (okey)
        return DictLookup(dict, (OObject *) okey.get(), out_value);

    return false;
}

bool orbiter::datatype::DictRemove(Dict *dict, OObject *key) {
    std::unique_lock _(dict->lock);

    ORHEntry *out;

    if (!dict->dict.Remove(key, &out))
        return false;

    dict->dict.FreeHEntry(out);

    return true;
}

bool orbiter::datatype::DictRemove(Dict *dict, const char *key) {
    const auto okey = ORStringNew(O_GET_ISOLATE(dict), key);

    if (okey)
        return DictRemove(dict, (OObject *) okey.get());

    return false;
}

bool orbiter::datatype::DictTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) DictDtor;
    self->trace = (TraceFn) DictTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.contains = DictOpContains;
    ops.equal = DictEqual;
    ops.to_bool = DictToBool;
    ops.to_string = (ToStrFn) DictToString;

    return TIPropertyAdd(self, dict_methods, PropertyFlag::IS_PUBLIC);
}

HDict orbiter::datatype::DictNew(Isolate *isolate, const U32 size) {
    auto *dict = MakeObject<Dict>(isolate, InstanceType::DICT);

    if (dict != nullptr) {
        new(&dict->dict)ORHMap(isolate);

        const auto ok = size > 0 ? dict->dict.Initialize(size) : dict->dict.Initialize();
        if (!ok) {
            isolate->gc->RawFree((OObject *) dict, false);

            return {};
        }

        new(&dict->lock)sync::AsyncRWLock();
    }

    O_GC_TRACK_RETURN(isolate, dict, true);
}

HDict orbiter::datatype::DictNew(OObject *object) {
    if (O_IS_SMI(object))
        assert(false);

    auto *isolate = O_GET_ISOLATE(object);

    if (O_IS_TYPE(object, InstanceType::DICT)) {
        auto *other = (Dict *) object;

        std::shared_lock _(other->lock);

        auto copy = DictNew(isolate, (U32) other->dict.length);
        if (!copy)
            return {};

        const auto copy_raw = copy.get();
        for (const auto *cur = other->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
            if (!DictInsert(copy_raw, cur->key, cur->value))
                return {};
        }

        return copy;
    }

    if (O_IS_TYPE(object, InstanceType::LIST)) {
        assert(false);
    }

    assert(false);
}

HOObject orbiter::datatype::DictKeys(Dict *dict) {
    auto *isolate = O_GET_ISOLATE(dict);

    std::shared_lock _(dict->lock);

    auto list = ListNew(isolate, dict->dict.length);
    if (!list)
        return {};

    for (const auto *cur = dict->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!ListAppend(list.get(), cur->key))
            return {};
    }

    return HOObject(std::move(list));
}

HOType orbiter::datatype::DictTypeInit(Isolate *isolate) {
    auto dict = MakeType(isolate, "Dict", InstanceType::DICT, sizeof(Dict) - sizeof(OObject), 13, 0);
    return dict;
}
