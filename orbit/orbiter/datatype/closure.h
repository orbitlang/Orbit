// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_CLOSURE_H_
#define ORBIT_ORBITER_DATATYPE_CLOSURE_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

namespace orbiter::datatype {
    struct Closure {
        OROBJ_HEAD;

        Closure *parent;

        sync::AsyncRWLock lock;

        U16 slots;
    };

    using HClosure = Handle<Closure>;

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
    bool ClosureTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new closure object in the given isolate with the specified number of slots
     *
     * This function allocates and initializes a new closure object associated with an isolate.
     * The total size of the closure is determined by the provided number of slots, where each
     * slot corresponds to a pointer-sized storage. Additionally, the function ensures the closure
     * is tracked appropriately for garbage collection purposes within the isolate.
     *
     * @param isolate Pointer to the isolate context within which the closure will be created
     * @param parent Closure of the enclosing function, reached by following the parent link; may be null
     * @param slots The number of slots (pointer-sized units) to allocate for the closure object
     *
     * @return A handle to the newly created closure object, or a null handle if allocation fails
     */
    HClosure ClosureNew(Isolate *isolate, Closure *parent, U16 slots);

    /**
     * @brief Retrieves an object from a closure, or from one of its parents
     *
     * This function follows the parent link `depth` times, then reads the slot at
     * `index` of the closure it reached. The parent links never change once a
     * closure is created, so only the final read takes the closure's shared lock.
     *
     * @param closure Pointer to the closure held by the running frame
     * @param depth How many parent links to follow (0 reads `closure` itself)
     * @param index The zero-based slot to read in the closure reached
     *
     * @return The retrieved object as an HOObject
     */
    HOObject ClosureGet(Closure *closure, U16 depth, U16 index);

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
    HOType ClosureTypeInit(Isolate *isolate);

    /**
     * @brief Assigns an object to a slot of a closure, or of one of its parents.
     *
     * This function follows the parent link `depth` times, then updates the slot at
     * `index` of the closure it reached, holding that closure's lock for the write.
     *
     * @param closure Pointer to the closure held by the running frame.
     * @param depth How many parent links to follow (0 writes `closure` itself).
     * @param index The zero-based slot to update in the closure reached.
     * @param object Pointer to the object to be assigned to the slot.
     */
    void ClosureSet(Closure *closure, U16 depth, U16 index, OObject *object);
}

#endif // !ORBIT_ORBITER_DATATYPE_CLOSURE_H_
