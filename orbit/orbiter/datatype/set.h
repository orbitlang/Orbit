// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_SET_H_
#define ORBIT_ORBITER_DATATYPE_SET_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/hashmap.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

namespace orbiter::datatype {
    struct Set {
        OROBJ_HEAD;

        sync::AsyncRWLock lock;

        ORHMap set;
    };

    using HSet = Handle<Set>;

    /**
     * @brief Updates the first set by removing elements that are present in the second set
     *
     * This function modifies the given set `self` by removing all elements that are also
     * present in the set `other`. If `self` and `other` are the same, the `self` set is cleared.
     *
     * @param self Pointer to the set that will be updated
     * @param other Pointer to the set containing elements to remove from `self`
     *
     * @return true if the operation was successful, false if an error occurred during element removal
     */
    bool SetDifferenceUpdate(Set *self, Set *other);

    /**
     * @brief Updates the first set by retaining only the elements that are also present in the second set
     *
     * This function modifies the given set `self` by removing all elements that are not present in the set `other`.
     * If `self` and `other` refer to the same set, the function returns immediately without making any changes.
     *
     * @param self Pointer to the set that will be updated
     * @param other Pointer to the set used for intersection comparison
     *
     * @return true if the operation was successful, false if an error occurred during execution
     */
    bool SetIntersectionUpdate(Set *self, Set *other);

    /**
     * @brief Determines whether two sets are disjoint
     *
     * This function checks if there are no common elements between the specified sets.
     * For optimization, the smaller set is probed against the larger set to minimize the work required.
     * If the sets are the same, the function considers them disjoint only if they are empty.
     *
     * @param a Pointer to the first set
     * @param b Pointer to the second set
     *
     * @return true if the sets are disjoint (no common elements), false otherwise
     */
    bool SetIsDisjoint(Set *a, Set *b);

    /**
     * @brief Determines whether the first set is a subset of the second set
     *
     * This function checks whether all elements of the first set are contained within the second set.
     *
     * @param a Pointer to the first set (potential subset)
     * @param b Pointer to the second set (potential superset)
     *
     * @return true if all elements in set `a` are present in set `b`, false otherwise
     */
    bool SetIsSubset(Set *a, Set *b);

    /**
     * @brief Checks if one set is a superset of another set
     *
     * This function determines whether set `a` is a superset of set `b`.
     * The relation `a ⊇ b` is evaluated by deferring to the canonical implementation
     * that checks if `b` is a subset of `a`.
     *
     * @param a Pointer to the candidate superset
     * @param b Pointer to the candidate subset
     *
     * @return true if `a` is a superset of `b`, false otherwise
     */
    bool SetIsSuperset(Set *a, Set *b);

    /**
     * @brief Set up additional features and properties for the specified type
     *
     * This function enriches the previously created type with various functionalities.
     * It typically performs the following tasks:
     * - Adds default methods to the type
     * - Adds required properties to the type
     *
     * This function is called immediately after the type's Init function to complete its setup.
     *
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool SetTypeSetup(TypeInfo *self);

    /**
     * @brief Updates the first set by applying the symmetric difference with the second set
     *
     * This function modifies the given set `self` by applying the symmetric difference operation
     * with the set `other`. The symmetric difference includes elements that are in either one of the
     * sets but not in their intersection. If `self` and `other` are the same set, `self` is cleared.
     *
     * @param self Pointer to the set that will be updated
     * @param other Pointer to the set used to compute the symmetric difference with `self`
     *
     * @return true if the operation was successful, false if an error occurred during element modifications
     */
    bool SetSymmetricDifferenceUpdate(Set *self, Set *other);

    /**
     * @brief Updates the first set by adding elements from the second set
     *
     * This function modifies the given set `self` by adding all elements that are present
     * in the set `other`. If `self` and `other` are the same, no changes are made and the
     * function immediately returns true.
     *
     * @param self Pointer to the set that will be updated
     * @param other Pointer to the set containing elements to add to `self`
     *
     * @return true if the operation was successful, false if an error occurred while adding elements
     */
    bool SetUnionUpdate(Set *self, Set *other);

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Handle to the newly created TypeInfo for the type, or an empty handle if creation failed
     */
    HOType SetTypeInit(Isolate *isolate);

    /**
     * @brief Computes the set difference of two sets
     *
     * This function returns a new set containing all elements that are present
     * in the set `a` but not in the set `b`. The original sets `a` and `b` remain
     * unmodified. If both `a` and `b` point to the same set, an empty set is returned.
     *
     * @param a Pointer to the first set
     * @param b Pointer to the second set
     *
     * @return A handle to the new set containing the difference of `a` and `b`,
     *         or an empty handle if an error occurs during the operation
     */
    HSet SetDifference(Set *a, Set *b);

    /**
     * @brief Computes the intersection of two sets
     *
     * This function creates a new set containing the elements that are present in both
     * input sets `a` and `b`. If the two sets are identical, a copy of the set is returned.
     *
     * @param a Pointer to the first set
     * @param b Pointer to the second set
     *
     * @return A handle to the new set containing the intersection of `a` and `b`, or an empty handle
     *         if an error occurred during allocation or element operations.
     */
    HSet SetIntersection(Set *a, Set *b);

    /**
     * @brief Creates a new Set instance with optional size initialization
     *
     * This function allocates and initializes a new Set object in the specified isolate.
     * If the allocation or initialization fails, the function cleans up and returns an empty handle.
     *
     * @param isolate The isolate in which the Set instance will be created
     * @param size Optional parameter specifying the initial size of the internal hash map. If the size is 0, a default initialization is performed.
     *
     * @return A handle to the newly created Set instance, or an empty handle if the creation fails
     */
    HSet SetNew(Isolate *isolate, U32 size);

    /**
     * @brief Creates a new set with an optional initial capacity
     *
     * This function initializes and returns a new set associated with the given target
     * `isolate`. If no initial capacity is specified, it defaults to 0.
     *
     * @param isolate Pointer to the isolate with which the new set will be associated
     *
     * @return A new set initialized for the provided isolate
     */
    inline HSet SetNew(Isolate *isolate) {
        return SetNew(isolate, 0);
    }

    /**
     * @brief Creates a new set based on the input object's type and contents
     *
     * This function initializes and returns a new set populated with elements extracted
     * from the provided object. The behavior varies depending on the type of the input
     * object:
     * - If the object is a dictionary (`DICT`), the set will be populated with the dictionary's keys.
     * - If the object is a list (`LIST`), the set will be populated with the list's elements.
     * - If the object is a set (`SET`), a copy of the set will be created.
     * - If the object is a tuple (`TUPLE`), the set will be populated with the tuple's elements.
     *
     * If the object is of a type not listed above, this function will log an error
     * indicating a type mismatch and return an empty handle.
     *
     * @param object Pointer to the object from which the new set will be created.
     * Must be of type `Dict`, `List`, `Set`, or `Tuple`.
     *
     * @return A handle to the newly created set, or an empty handle if the operation
     * failed due to invalid input or allocation errors.
     */
    HSet SetNew(OObject *object) noexcept;

    /**
     * @brief Creates a new set object from an existing set
     *
     * @param set Pointer to the existing set to be used for creating the new set
     *
     * @return A new set object if successful, or an uninitialized set if an error occurs
     */
    inline HSet SetNew(Set *set) noexcept {
        return SetNew((OObject *) set);
    }

    /**
     * @brief Computes the symmetric difference of two sets.
     *
     * This function calculates the symmetric difference of sets `a` and `b`.
     * The symmetric difference includes all elements that are in either of the
     * sets but not in their intersection. The resulting set is created and returned
     * without modifying the input sets.
     *
     * @param a Pointer to the first set.
     * @param b Pointer to the second set.
     *
     * @return A handle to a newly created set containing the symmetric difference
     * of `a` and `b`. Returns an empty handle if an error occurs during computation.
     */
    HSet SetSymmetricDifference(Set *a, Set *b);

    /**
     * @brief Computes the union of two sets and returns a new set containing all unique elements
     *
     * This function creates a new set that includes all elements from the input sets `a` and `b`.
     * If the two input sets are the same, a new set with the same elements as `a` is returned.
     * Both input sets are left unmodified.
     *
     * @param a Pointer to the first set
     * @param b Pointer to the second set
     *
     * @return A handle to the new set containing the union of elements from both `a` and `b`.
     *         Returns an empty handle if an error occurs during the operation.
     */
    HSet SetUnion(Set *a, Set *b);

    /**
     * @brief Adds a value to the set in a thread-safe manner
     *
     * This function attempts to add the specified `value` to the given `set`.
     *
     * @param set Pointer to the set where the value will be added
     * @param value Pointer to the value to add to the set
     *
     * @return A `LookupResult` indicating the result of the operation, which can be one of:
     *         - OK: The value was successfully added
     *         - NOT_FOUND: The value was not added because it already exists
     *         - ERROR: An error occurred during the addition
     */
    LookupResult SetAdd(Set *set, OObject *value);

    /**
     * @brief Checks if a specified value exists within a set.
     *
     * This function verifies whether the given `value` is present in the specified `set`.
     *
     * @param set Pointer to the set to search within.
     * @param value Pointer to the object to look for in the set.
     *
     * @return A `LookupResult` indicating the outcome of the operation:
     *         - `OK` if the value is found.
     *         - `NOT_FOUND` if the value is not present in the set.
     *         - `ERROR` if an error occurs during lookup.
     */
    LookupResult SetContains(Set *set, OObject *value);

    /**
     * @brief Removes a specific value from a given set
     *
     * This function attempts to remove the specified value from the provided set.
     *
     * @param set Pointer to the set from which the value will be removed
     * @param value Pointer to the value that needs to be removed from the set
     *
     * @return LookupResult indicating the result of the removal operation. Possible values are:
     *         - LookupResult::OK if the value was successfully removed
     *         - LookupResult::NOT_FOUND if the value was not found in the set
     *         - LookupResult::ERROR if an error occurred during the operation
     */
    LookupResult SetRemove(Set *set, OObject *value);
}

#endif // !ORBIT_ORBITER_DATATYPE_SET_H_
