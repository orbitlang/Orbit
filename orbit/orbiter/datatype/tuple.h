// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_TUPLE_H_
#define ORBIT_ORBITER_DATATYPE_TUPLE_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/datatype/list.h>

namespace orbiter::datatype {
    constexpr MSSize kTupleContainsError = -2;

    struct Tuple {
        OROBJ_HEAD;

        OObject **objects;

        MSize capacity;

        MSize length;

        MSize hash;
    };

    using HTuple = Handle<Tuple>;

    /**
     * @brief Adds an item to the specified tuple
     *
     * This function appends a new item to the provided tuple, if there is sufficient capacity.
     * If the append operation is successful, the length of the tuple is incremented, and
     * the reference count for the added item is increased.
     *
     * If the provided tuple has reached its maximum capacity, the function fails
     * without appending the item.
     *
     * @note This function should not be used for any reason outside creating a new tuple.
     * Do not use it on tuples created outside the context where TupleNew was invoked.
     *
     * @param tuple Pointer to the Tuple object where the item will be appended
     * @param item Pointer to the OObject to be added to the tuple
     *
     * @return true if the item was successfully appended, false if the tuple is at capacity
     */
    bool TupleAppend(Tuple *tuple, OObject *item);

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
    bool TupleTypeSetup(TypeInfo *self);

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
    HOType TupleTypeInit(Isolate *isolate);

    /**
     * @brief Creates a new Tuple object with the specified length
     *
     * This function initializes a new Tuple object in the provided isolate with the given number
     * of elements. If the allocation fails at any step, the function returns an empty handle,
     * and appropriate cleanup is performed.
     *
     * The resulting Tuple can store references to objects up to the specified count.
     * The hash field is initialized to zero.
     *
     * @param isolate Pointer to the Isolate in which the Tuple object will be created
     * @param count The number of elements to allocate within the Tuple
     *
     * @return A Handle to the newly created Tuple object if allocation is successful,
     *         or an empty Handle if any part of the creation process fails
     */
    HTuple TupleNew(Isolate *isolate, MSize count);

    /**
     * @brief Creates a new tuple from the provided object.
     *
     * This function creates and returns a new tuple based on the type of the given object.
     * For unsupported object types or invalid inputs, the function returns an empty tuple handle.
     *
     * @param object Pointer to an OObject used as the base for creating the tuple.
     *
     * @return A handle to the newly created tuple. Returns an empty handle if the operation fails
     *         or if the input type is unsupported.
     */
    HTuple TupleNew(OObject *object);

    /**
     * @brief Creates a new Tuple object from the given list of objects
     *
     * This function initializes a Tuple object using the properties from the provided
     * HList instance. The new Tuple will inherit the objects, capacity, and length of the
     * original list, while the HList's internal data will be reset. The Tuple is returned
     * as a handle (HTuple) for further usage.
     *
     * The function ensures proper memory management by transferring ownership of the list's
     * content to the newly created Tuple and resetting the list state.
     *
     * @warning This is a convenience function, using this function incorrectly causes an IMMEDIATE CRASH.
     *
     * @param list Reference to the HList object used to create the Tuple
     *
     * @return A handle to the newly created Tuple object, or nullptr if initialization fails
     */
    HTuple TupleNewFromList(HList &list);

    /**
     * @brief Return the index of the first element equal to @p value, or -1.
     *
     * Performs a linear scan using structural equality (Equal).
     * Callers that only need a presence check can compare the result with -1.
     *
     * @param tuple  The tuple to search.
     * @param value  The value to look for.
     *
     * @return The index of the first match, -1 if not found, or
     *         kTupleContainsError if an equality hook panicked (the panic is
     *         already set; the caller must propagate the failure).
     */
    MSSize TupleContains(const Tuple *tuple, const OObject *value);
}

#endif // !ORBIT_ORBITER_DATATYPE_TUPLE_H_
