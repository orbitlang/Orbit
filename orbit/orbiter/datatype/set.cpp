// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/rguard.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/datatype/set.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool SetDtor(Set *self) {
    self->set.Finalize(nullptr);
    self->set.~ORHMap();

    self->lock.~AsyncRWLock();

    return true;
}

/// Both operands of a binary set op must be Sets — Dispatch may call us
/// with `left` non-Set when this op was reached as a fallback from the
/// right operand's TypeOps. Returning false without setting a panic lets
/// Dispatch synthesise the canonical NotImplementedError(op, lname, rname).
static bool SetOpRequireBoth(const OObject *left, const OObject *right) {
    if (!O_IS_OBJECT(left) || !O_IS_TYPE(left, InstanceType::SET))
        return false;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::SET))
        return false;

    return true;
}

/// Insert one element into `dst` without acquiring its lock.
/// Caller MUST hold `dst->lock` in unique mode.
static LookupResult SetAddLocked(Set *dst, OObject *value) {
    ORHEntry *entry;

    const auto status = dst->set.Lookup(value, &entry);
    if (status == LookupResult::ERROR)
        return LookupResult::ERROR;

    if (status == LookupResult::NOT_FOUND) {
        entry = dst->set.AllocHEntry();
        if (entry == nullptr)
            return LookupResult::ERROR;

        entry->key = value;
        entry->value = nullptr;

        if (dst->set.Insert(entry) != LookupResult::OK) {
            dst->set.FreeHEntry(entry);

            return LookupResult::ERROR;
        }
    }

    return LookupResult::OK;
}

bool SetExtendFromArray(Set *dst, OObject *const *src, const MSize count) {
    for (MSize i = 0; i < count; i++) {
        if (SetAddLocked(dst, src[i]) == LookupResult::ERROR)
            return false;
    }
    return true;
}

/// Like SetAddLocked but for a single Lookup-only check (no insertion path).
/// Returns OK / NOT_FOUND / ERROR.
static LookupResult SetContainsLocked(const Set *src, OObject *value) {
    ORHEntry *entry = nullptr;

    return src->set.Lookup(value, &entry);
}

/// Remove without locking — caller MUST hold `dst->lock` in unique mode.
static LookupResult SetRemoveLocked(Set *dst, OObject *value) {
    ORHEntry *out;

    const auto status = dst->set.Remove(value, &out);
    if (status == LookupResult::OK)
        dst->set.FreeHEntry(out);

    return status;
}

void SetTrace(const Set *self, const GCTraceCallback callback, const MSize epoch) {
    // The `value` slot of every entry is unused (always nullptr) — only keys need to be reported to the GC.
    for (const auto *cursor = self->set.iter_begin; cursor != nullptr; cursor = cursor->iter_next)
        callback((OObject *) cursor->key, epoch);
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Two sets are equal when they have the same length and every element of
/// `left` exists in `right`. Uses the generic Equal() dispatch via the
/// underlying ORHMap, so element types provide their own equality.
static bool SetEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::SET))
        return false;

    auto *a = (Set *) left;
    auto *b = (Set *) right;

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    if (a->set.length != b->set.length)
        return false;

    for (const auto *cur = a->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        ORHEntry *entry;

        // Same length + a ⊆ b ⇒ a == b.
        if (b->set.Lookup(cur->key, &entry) != LookupResult::OK)
            return false;
    }

    return true;
}

/// Membership test: `x in s`. Mirrors DictOpContains: ERROR (e.g. unhashable
/// element) is propagated, NOT_FOUND becomes a clean false.
static bool SetOpContains(const OObject *container, const OObject *value, bool &result) {
    switch (SetContains((Set *) container, (OObject *) value)) {
        case LookupResult::OK:
            result = true;
            return true;
        case LookupResult::NOT_FOUND:
            result = false;
            return true;
        case LookupResult::ERROR:
            return false;
    }

    return false;
}

// *********************************************************************************************************************
// TYPE OPS — ARITHMETIC
// *********************************************************************************************************************

/// `a - b` — set difference.
static bool SetOpSub(const OObject *left, const OObject *right, OObject *&result) {
    if (!SetOpRequireBoth(left, right))
        return false;

    const auto out = SetDifference((Set *) left, (Set *) right);
    if (!out)
        return false;

    result = (OObject *) out.get();
    return true;
}

// *********************************************************************************************************************
// TYPE OPS — BITWISE
// *********************************************************************************************************************

/// `a & b` — intersection of two sets.
static bool SetOpBitAnd(const OObject *left, const OObject *right, OObject *&result) {
    if (!SetOpRequireBoth(left, right))
        return false;

    const auto out = SetIntersection((Set *) left, (Set *) right);
    if (!out)
        return false;

    result = (OObject *) out.get();
    return true;
}

/// `a | b` — union of two sets.
static bool SetOpBitOr(const OObject *left, const OObject *right, OObject *&result) {
    if (!SetOpRequireBoth(left, right))
        return false;

    auto out = SetUnion((Set *) left, (Set *) right);
    if (!out)
        return false;

    result = (OObject *) out.get();
    return true;
}

/// `a ^ b` — symmetric difference.
static bool SetOpBitXor(const OObject *left, const OObject *right, OObject *&result) {
    if (!SetOpRequireBoth(left, right))
        return false;

    auto out = SetSymmetricDifference((Set *) left, (Set *) right);
    if (!out)
        return false;

    result = (OObject *) out.get();
    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-empty set is truthy. Length is read without the lock — same trade-off as Dict's to_bool.
static bool SetToBool(const OObject *self) {
    return ((const Set *) self)->set.length != 0;
}

/// Empty set renders as `Set{}`; non-empty as `Set{e1, e2, ...}`. Self-cycles
/// are handled via the fiber-local ReprGuard, mirroring DictToString.
static OObject *SetToString(orbiter::Isolate *isolate, Set *self) {
    const ReprGuard guard((OObject *) self);
    if (guard.IsError())
        return nullptr;

    if (guard.IsCyclic())
        return (OObject *) ORStringNew(isolate, "{...}", 5).get();

    std::shared_lock _(self->lock);

    StringBuilder builder(isolate);

    constexpr unsigned char prefix[] = {'{'};
    constexpr unsigned char close_brace[] = {'}'};
    constexpr unsigned char item_sep[] = {',', ' '};

    // Rough hint: 5 bytes per element for the initial buffer, plus prefix/close.
    if (!builder.Write(prefix, 1, self->set.length * 5 + 5))
        return nullptr;

    bool first = true;
    for (const auto *cur = self->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (!first && !builder.Write(item_sep, 2, 0))
            return nullptr;

        first = false;

        auto v = Repr(isolate, cur->key);
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

RUNTIME_FUNCTION(set_create, create,
                 R"DOC(
@brief Create a new Set.

When `base` is provided, the returned set is initialised with the elements
of `base`, which must be a Dict, List, a Tuple or another Set. Duplicates in the
source are collapsed to a single occurrence. Without `base`, the returned
set is empty.

@param base?  Source to initialise from. Must be a Dict, List, Tuple or Set.

@return A new Set.

@panic TypeError  When `base` is not a List, a Tuple or a Set.

@see add, length

@example
    Set()                 // Set{}
    Set([1, 2, 2, 3])     // Set{1, 2, 3}
    Set((4, 5))           // Set{4, 5}
    let s = Set([1, 2])
    Set(s)                // Set{1, 2}  — copy
)DOC", 0, "base", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("base", true,
                       InstanceType::DICT,
                       InstanceType::LIST,
                       InstanceType::TUPLE,
                       InstanceType::SET));
    PCHECK_CHECK(params);

    const auto set = O_IS_SENTINEL(argv[0]) ? SetNew(O_GET_ISOLATE(_func)) : SetNew(argv[0]);
    if (!set)
        return {};

    return HOObject((OObject *) set.get());
}

RUNTIME_METHOD(set_add, add,
               R"DOC(
@brief Insert an element into the set.

If `value` is already present the set is left unchanged.

@param value  The element to add.

@panic TypeError  When `value` is unhashable.

@see remove, discard, contains

@example
    let s = Set()
    s.add(1)
    s.add(1)        // no-op
    s.length()      // 1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("value", false));
    PCHECK_CHECK(params);

    if (SetAdd((Set *) argv[0], argv[1]) == LookupResult::ERROR)
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(set_clear, clear,
               R"DOC(
@brief Remove all elements from the set in-place.

@see copy, is_empty

@example
    let s = Set()
    s.add(1)
    s.clear()
    s.is_empty()    // true
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::SET));
    PCHECK_CHECK(params);

    auto *self = (Set *) argv[0];

    std::unique_lock _(self->lock);

    self->set.Clear(nullptr);

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(set_contains, contains,
               R"DOC(
@brief Return true when the set contains the given value.

@param value  The element to test.

@return true if the value is in the set, false otherwise.

@see add, remove

@example
    let s = Set()
    s.add(1)
    s.contains(1)    // true
    s.contains(2)    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("value", false));
    PCHECK_CHECK(params);

    switch (SetContains((Set *) argv[0], argv[1])) {
        case LookupResult::OK:
            return HOObject((OObject *) BOOL_TO_OBOOL(true));
        case LookupResult::NOT_FOUND:
            return HOObject((OObject *) BOOL_TO_OBOOL(false));
        case LookupResult::ERROR:
            return {};
    }

    return {};
}

RUNTIME_METHOD(set_difference, difference,
               R"DOC(
@brief Return a new set with elements in self but not in other.

Equivalent to `self - other` in set algebra.

@param other  The set to subtract.

@return A new Set containing elements of self not present in other.

@panic TypeError  When `other` is not a Set.

@see difference_update, intersection, union

@example
    let a = Set() ; a.add(1) ; a.add(2) ; a.add(3)
    let b = Set() ; b.add(2)
    a.difference(b)    // Set{1, 3}
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    const auto out = SetDifference((Set *) argv[0], (Set *) argv[1]);
    if (!out)
        return {};

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(set_difference_update, difference_update,
               R"DOC(
@brief Remove from self every element also present in other.

@param other  The set whose elements are removed from self.

@panic TypeError  When `other` is not a Set.

@see difference, intersection_update

@example
    let a = Set() ; a.add(1) ; a.add(2)
    let b = Set() ; b.add(2)
    a.difference_update(b)
    a.contains(2)    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    if (!SetDifferenceUpdate((Set *) argv[0], (Set *) argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(set_discard, discard,
               R"DOC(
@brief Remove an element from the set if present.

Unlike `remove`, this method does not raise an error when the element is
absent.

@param value  The element to remove.

@return true if the element was removed, false if it was not present.

@panic TypeError  When `value` is unhashable.

@see remove, add

@example
    let s = Set() ; s.add(1)
    s.discard(1)    // true
    s.discard(1)    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("value", false));
    PCHECK_CHECK(params);

    switch (SetRemove((Set *) argv[0], argv[1])) {
        case LookupResult::OK:
            return HOObject((OObject *) BOOL_TO_OBOOL(true));
        case LookupResult::NOT_FOUND:
            return HOObject((OObject *) BOOL_TO_OBOOL(false));
        case LookupResult::ERROR:
            return {};
    }

    return {};
}

RUNTIME_METHOD(set_intersection, intersection,
               R"DOC(
@brief Return a new set with elements common to self and other.

Equivalent to `self & other` in set algebra.

@param other  The set to intersect with.

@return A new Set containing only elements present in both self and other.

@panic TypeError  When `other` is not a Set.

@see intersection_update, union, difference

@example
    let a = Set() ; a.add(1) ; a.add(2)
    let b = Set() ; b.add(2) ; b.add(3)
    a.intersection(b)    // Set{2}
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    const auto out = SetIntersection((Set *) argv[0], (Set *) argv[1]);
    if (!out)
        return {};

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(set_intersection_update, intersection_update,
               R"DOC(
@brief Keep in self only the elements that are also in other.

@param other  The set to intersect with.

@panic TypeError  When `other` is not a Set.

@see intersection, difference_update

@example
    let a = Set() ; a.add(1) ; a.add(2)
    let b = Set() ; b.add(2) ; b.add(3)
    a.intersection_update(b)
    a.contains(1)    // false
    a.contains(2)    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    if (!SetIntersectionUpdate((Set *) argv[0], (Set *) argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(set_is_disjoint, is_disjoint,
               R"DOC(
@brief Return true when self and other share no elements.

@param other  The set to compare against.

@return true if the intersection is empty, false otherwise.

@panic TypeError  When `other` is not a Set.

@see is_subset, is_superset

@example
    let a = Set() ; a.add(1)
    let b = Set() ; b.add(2)
    a.is_disjoint(b)    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(SetIsDisjoint((Set *) argv[0], (Set *) argv[1])));
}

RUNTIME_METHOD(set_is_empty, is_empty,
               R"DOC(
@brief Return true when the set has no elements.

@return true if empty, false otherwise.

@see length, clear

@example
    let s = Set()
    s.is_empty()    // true
    s.add(1)
    s.is_empty()    // false
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::SET));
    PCHECK_CHECK(params);

    const auto *self = (const Set *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(self->set.length == 0));
}

RUNTIME_METHOD(set_is_subset, is_subset,
               R"DOC(
@brief Return true when every element of self is also in other.

@param other  The candidate superset.

@return true if self ⊆ other, false otherwise.

@panic TypeError  When `other` is not a Set.

@see is_superset, is_disjoint

@example
    let a = Set() ; a.add(1)
    let b = Set() ; b.add(1) ; b.add(2)
    a.is_subset(b)    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(SetIsSubset((Set *) argv[0], (Set *) argv[1])));
}

RUNTIME_METHOD(set_is_superset, is_superset,
               R"DOC(
@brief Return true when self contains every element of other.

@param other  The candidate subset.

@return true if self ⊇ other, false otherwise.

@panic TypeError  When `other` is not a Set.

@see is_subset, is_disjoint

@example
    let a = Set() ; a.add(1) ; a.add(2)
    let b = Set() ; b.add(1)
    a.is_superset(b)    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(SetIsSuperset((Set *) argv[0], (Set *) argv[1])));
}

RUNTIME_METHOD(set_length, length,
               R"DOC(
@brief Return the number of elements in the set.

@return A non-negative Int.

@see is_empty

@example
    let s = Set()
    s.add(1)
    s.add(2)
    s.length()    // 2
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::SET));
    PCHECK_CHECK(params);

    const auto *self = (const Set *) argv[0];

    auto n = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) self->set.length);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(set_remove, remove,
               R"DOC(
@brief Remove an element from the set.

Raises a KeyError if the element is not present. Use `discard` if you want
silent removal.

@param value  The element to remove.

@panic KeyError   When `value` is not in the set.
@panic TypeError  When `value` is unhashable.

@see discard, add

@example
    let s = Set() ; s.add(1)
    s.remove(1)
    s.remove(1)    // panic — KeyError
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("value", false));
    PCHECK_CHECK(params);

    auto *self = (Set *) argv[0];

    switch (SetRemove(self, argv[1])) {
        case LookupResult::OK:
            return HOObject(kOddBallNIL);
        case LookupResult::NOT_FOUND:
            ErrorSet(O_GET_ISOLATE(self),
                     KeyError::Details[KeyError::Reason::ID],
                     nullptr,
                     KeyError::Details[KeyError::Reason::NOT_FOUND],
                     O_GET_TYPE(self)->name);

            return {};
        case LookupResult::ERROR:
            return {};
    }

    return {};
}

RUNTIME_METHOD(set_symmetric_difference, symmetric_difference,
               R"DOC(
@brief Return a new set with elements in either self or other, but not both.

Equivalent to `self ^ other` in set algebra.

@param other  The set to combine with.

@return A new Set containing the symmetric difference.

@panic TypeError  When `other` is not a Set.

@see symmetric_difference_update, union, intersection

@example
    let a = Set() ; a.add(1) ; a.add(2)
    let b = Set() ; b.add(2) ; b.add(3)
    a.symmetric_difference(b)    // Set{1, 3}
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    const auto out = SetSymmetricDifference((Set *) argv[0], (Set *) argv[1]);
    if (!out)
        return {};

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(set_symmetric_difference_update, symmetric_difference_update,
               R"DOC(
@brief Update self to contain the symmetric difference with other.

After the call self contains exactly the elements that were in self xor in
other.

@param other  The set to combine with.

@panic TypeError  When `other` is not a Set.

@see symmetric_difference, update

@example
    let a = Set() ; a.add(1) ; a.add(2)
    let b = Set() ; b.add(2) ; b.add(3)
    a.symmetric_difference_update(b)
    a.contains(1)    // true
    a.contains(2)    // false
    a.contains(3)    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    if (!SetSymmetricDifferenceUpdate((Set *) argv[0], (Set *) argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(set_union, union,
               R"DOC(
@brief Return a new set with elements from both self and other.

Equivalent to `self | other` in set algebra.

@param other  The set to combine with.

@return A new Set containing every element of self and other.

@panic TypeError  When `other` is not a Set.

@see update, intersection, difference

@example
    let a = Set() ; a.add(1)
    let b = Set() ; b.add(2)
    a.union(b)    // Set{1, 2}
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    const auto out = SetUnion((Set *) argv[0], (Set *) argv[1]);
    if (!out)
        return {};

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(set_update, update,
               R"DOC(
@brief Add every element of other to self.

@param other  The set whose elements are merged into self.

@panic TypeError  When `other` is not a Set.

@see union, add

@example
    let a = Set() ; a.add(1)
    let b = Set() ; b.add(2)
    a.update(b)
    a.length()    // 2
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::SET),
                   PCHECK_DEF("other", false, InstanceType::SET));
    PCHECK_CHECK(params);

    if (!SetUnionUpdate((Set *) argv[0], (Set *) argv[1]))
        return {};

    return HOObject(kOddBallNIL);
}

constexpr FunctionDef set_methods[] = {
    set_add,
    set_clear,
    set_contains,
    set_create,
    set_difference,
    set_difference_update,
    set_discard,
    set_intersection,
    set_intersection_update,
    set_is_disjoint,
    set_is_empty,
    set_is_subset,
    set_is_superset,
    set_length,
    set_remove,
    set_symmetric_difference,
    set_symmetric_difference_update,
    set_union,
    set_update,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::SetDifferenceUpdate(Set *self, Set *other) {
    if (self == other) {
        // self - self = ∅
        std::unique_lock _(self->lock);

        self->set.Clear(nullptr);

        return true;
    }

    std::unique_lock self_lock(self->lock);
    std::shared_lock other_lock(other->lock);

    // Iterate `other` and remove from `self`. Removing keys that came from
    // `other` (not from self's iter list) is safe: it doesn't disturb the
    // iteration we're walking.
    for (const auto *cur = other->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetRemoveLocked(self, cur->key);
        if (status == LookupResult::ERROR)
            return false;
    }

    return true;
}

bool orbiter::datatype::SetIntersectionUpdate(Set *self, Set *other) {
    if (self == other)
        return true;

    std::unique_lock self_lock(self->lock);
    std::shared_lock other_lock(other->lock);

    // Two-pass: first identify the elements to drop, then drop them.
    // Removing while iterating the same iter list is unsafe — iter_next of a
    // removed entry is reset by ORHMap::RemoveIterItem.

    const auto cap = self->set.length;
    if (cap == 0)
        return true;

    auto *isolate = O_GET_ISOLATE(self);

    memory::IsolateAllocator allocator(isolate);
    auto **doomed = allocator.alloc<OObject *>(cap * sizeof(void *));
    if (doomed == nullptr)
        return false;

    size_t doomed_count = 0;

    for (const auto *cur = self->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(other, cur->key);
        if (status == LookupResult::ERROR) {
            allocator.free(doomed);

            return false;
        }

        if (status == LookupResult::NOT_FOUND)
            doomed[doomed_count++] = cur->key;
    }

    for (size_t i = 0; i < doomed_count; i++)
        SetRemoveLocked(self, doomed[i]); // Cannot ERROR here: keys came from this same set, so they are hashable.

    allocator.free(doomed);

    return true;
}

bool orbiter::datatype::SetIsDisjoint(Set *a, Set *b) {
    if (a == b) {
        // Disjoint with self only when empty.
        std::shared_lock _(a->lock);

        return a->set.length == 0;
    }

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    // Probe the smaller side against the larger to minimise work.
    const Set *small = a->set.length <= b->set.length ? a : b;
    const Set *large = small == a ? b : a;

    for (const auto *cur = small->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(large, cur->key);
        if (status == LookupResult::OK)
            return false;
    }

    return true;
}

bool orbiter::datatype::SetIsSubset(Set *a, Set *b) {
    if (a == b)
        return true;

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    if (a->set.length > b->set.length)
        return false;

    for (const auto *cur = a->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(b, cur->key);
        if (status == LookupResult::NOT_FOUND)
            return false;
    }

    return true;
}

bool orbiter::datatype::SetIsSuperset(Set *a, Set *b) {
    // a ⊇ b  ⇔  b ⊆ a — defer to the canonical implementation.
    return SetIsSubset(b, a);
}

bool orbiter::datatype::SetTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) SetDtor;
    self->trace = (TraceFn) SetTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.contains = SetOpContains;
    ops.equal = SetEqual;
    ops.to_bool = SetToBool;
    ops.to_string = (ToStrFn) SetToString;

    // Set algebra as binary operators.
    ops.sub = SetOpSub; // a - b   (difference)
    ops.bit_and = SetOpBitAnd; // a & b   (intersection)
    ops.bit_or = SetOpBitOr; // a | b   (union)
    ops.bit_xor = SetOpBitXor; // a ^ b   (symmetric difference)

    if (!TIPropertyAdd(self, set_methods, PropertyFlag::IS_PUBLIC))
        return false;

    const auto ctor = FunctionFromDef(self, set_create);
    if (!ctor)
        return false;

    self->ctor = (OObject *) ctor.get();

    return true;
}

bool orbiter::datatype::SetSymmetricDifferenceUpdate(Set *self, Set *other) {
    if (self == other) {
        // self ^ self = ∅
        std::unique_lock _(self->lock);

        self->set.Clear(nullptr);

        return true;
    }

    std::unique_lock self_lock(self->lock);
    std::shared_lock other_lock(other->lock);

    // For each element in other: if present in self → remove from self,
    // otherwise → add to self. Keys come from other's iter list so the
    // walk over other is unaffected by mutations on self.
    for (const auto *cur = other->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(self, cur->key);
        switch (status) {
            case LookupResult::OK:
                if (SetRemoveLocked(self, cur->key) == LookupResult::ERROR)
                    return false;

                break;
            case LookupResult::NOT_FOUND:
                if (SetAddLocked(self, cur->key) == LookupResult::ERROR)
                    return false;

                break;
            case LookupResult::ERROR:
                return false;
        }
    }

    return true;
}

bool orbiter::datatype::SetUnionUpdate(Set *self, Set *other) {
    if (self == other)
        return true;

    std::unique_lock self_lock(self->lock);
    std::shared_lock other_lock(other->lock);

    for (const auto *cur = other->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (SetAddLocked(self, cur->key) == LookupResult::ERROR)
            return false;
    }

    return true;
}

HOType orbiter::datatype::SetTypeInit(Isolate *isolate) {
    return MakeType(isolate, "Set", InstanceType::SET, sizeof(Set) - sizeof(OObject), 20, 0);
}

HSet orbiter::datatype::SetNew(Isolate *isolate, const U32 size) {
    auto *set = MakeObject<Set>(isolate, InstanceType::SET);
    if (set != nullptr) {
        new(&set->set)ORHMap(isolate);

        const auto ok = size > 0 ? set->set.Initialize(size) : set->set.Initialize();
        if (!ok) {
            isolate->gc->RawFree((OObject *) set, false);

            return {};
        }

        new(&set->lock)sync::AsyncRWLock();
    }

    O_GC_TRACK_RETURN(isolate, set, true);
}

LookupResult orbiter::datatype::SetAdd(Set *set, OObject *value) {
    std::unique_lock _(set->lock);

    return SetAddLocked(set, value);
}

LookupResult orbiter::datatype::SetContains(Set *set, OObject *value) {
    std::shared_lock _(set->lock);

    return SetContainsLocked(set, value);
}

LookupResult orbiter::datatype::SetRemove(Set *set, OObject *value) {
    std::unique_lock _(set->lock);

    return SetRemoveLocked(set, value);
}

HSet orbiter::datatype::SetNew(OObject *object) noexcept {
    auto *isolate = O_GET_ISOLATE(object);

    if (O_IS_TYPE(object, InstanceType::DICT)) {
        auto *dict = (Dict *) object;

        std::shared_lock _(dict->lock);

        auto out = SetNew(isolate, (U32) dict->dict.length);
        if (!out)
            return {};

        auto *raw = out.get();
        for (const auto *cur = dict->dict.iter_begin; cur != nullptr; cur = cur->iter_next) {
            if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
                return {};
        }

        return out;
    }

    if (O_IS_TYPE(object, InstanceType::LIST)) {
        auto *list = (List *) object;

        std::shared_lock _(list->lock);

        auto out = SetNew(isolate, (U32) list->length);
        if (!out || !SetExtendFromArray(out.get(), list->objects, list->length))
            return {};

        return out;
    }

    if (O_IS_TYPE(object, InstanceType::SET)) {
        auto *self = (Set *) object;

        std::shared_lock _(self->lock);

        auto copy = SetNew(isolate, (U32) self->set.length);
        if (!copy)
            return {};

        auto *raw = copy.get();
        for (const auto *cur = self->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
            if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
                return {};
        }

        return copy;
    }

    if (O_IS_TYPE(object, InstanceType::TUPLE)) {
        const auto *tuple = (Tuple *) object;

        auto out = SetNew(isolate, (U32) tuple->length);
        if (!out || !SetExtendFromArray(out.get(), tuple->objects, tuple->length))
            return {};

        return out;
    }

    ErrorSetWithObjType(isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::MISMATCH],
                        "Dict, List, Tuple, or Set",
                        object);

    return {};
}

HSet orbiter::datatype::SetDifference(Set *a, Set *b) {
    auto *isolate = O_GET_ISOLATE(a);

    if (a == b)
        return SetNew(isolate);

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    auto out = SetNew(isolate, (U32) a->set.length);
    if (!out)
        return {};

    auto *raw = out.get();
    for (const auto *cur = a->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(b, cur->key);
        if (status == LookupResult::ERROR)
            return {};

        if (status == LookupResult::NOT_FOUND) {
            if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
                return {};
        }
    }

    return out;
}

HSet orbiter::datatype::SetIntersection(Set *a, Set *b) {
    auto *isolate = O_GET_ISOLATE(a);

    if (a == b)
        return SetNew(a);

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    // Iterate the smaller set and probe the larger one — better cache behaviour when sizes differ.
    const Set *small = a->set.length <= b->set.length ? a : b;
    const Set *large = small == a ? b : a;

    auto out = SetNew(isolate, (U32) small->set.length);
    if (!out)
        return {};

    auto *raw = out.get();
    for (const auto *cur = small->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(large, cur->key);
        if (status == LookupResult::ERROR)
            return {};

        if (status == LookupResult::OK) {
            if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
                return {};
        }
    }

    return out;
}

HSet orbiter::datatype::SetSymmetricDifference(Set *a, Set *b) {
    auto *isolate = O_GET_ISOLATE(a);

    if (a == b)
        return SetNew(isolate);

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    auto out = SetNew(isolate, (U32) (a->set.length + b->set.length));
    if (!out)
        return {};

    auto *raw = out.get();

    // Elements in a but not in b.
    for (const auto *cur = a->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(b, cur->key);
        if (status == LookupResult::ERROR)
            return {};

        if (status == LookupResult::NOT_FOUND) {
            if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
                return {};
        }
    }

    // Elements in b but not in a.
    for (const auto *cur = b->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        const auto status = SetContainsLocked(a, cur->key);
        if (status == LookupResult::ERROR)
            return {};

        if (status == LookupResult::NOT_FOUND) {
            if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
                return {};
        }
    }

    return out;
}

HSet orbiter::datatype::SetUnion(Set *a, Set *b) {
    auto *isolate = O_GET_ISOLATE(a);

    if (a == b)
        return SetNew(a);

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    auto out = SetNew(isolate, (U32) (a->set.length + b->set.length));
    if (!out)
        return {};

    auto *raw = out.get();

    for (const auto *cur = a->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
            return {};
    }

    for (const auto *cur = b->set.iter_begin; cur != nullptr; cur = cur->iter_next) {
        if (SetAddLocked(raw, cur->key) == LookupResult::ERROR)
            return {};
    }

    return out;
}
