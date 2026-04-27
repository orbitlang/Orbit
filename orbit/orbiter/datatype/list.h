// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_LIST_H_
#define ORBIT_ORBITER_DATATYPE_LIST_H_

#include <orbit/orbiter/sync/asyncrwlock.h>

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    constexpr int kListInitialCapacity = 24;

    struct List {
        OROBJ_HEAD;

        OObject **objects;

        MSize capacity;

        MSize length;

        sync::AsyncRWLock lock;
    };

    using HList = Handle<List>;

    /**
     * @brief Append object to the list.
     *
     * @param list List object.
     * @param object Object to append.
     * @return True on success, in case of error false will be returned and the panic state will be set.
     */
    bool ListAppend(List *list, OObject *object);

    /**
     * @brief Appends the contents of one list to another
     *
     * This function takes all elements from the `other` list and appends them
     * to the end of the `list`. It ensures that the destination list has
     * enough capacity to accommodate the new elements. Each appended element
     * has its reference count incremented to maintain proper memory management.
     *
     * If the `other` list is null, the function does nothing and returns true.
     *
     * @param list Pointer to the destination List to which the elements will be appended
     * @param other Pointer to the source List whose elements will be appended to the destination List
     *
     * @return true if the appending was successful or if the `other` list is null,
     * false if the destination list lacks sufficient capacity to store the new elements.
     */
    bool ListAppend(List *list, List *other);

    /**
     * @brief Extends the contents of a list by appending elements from another container object
     *
     * This function adds the elements of a given object to the specified list.
     *
     * @param list Pointer to the target list that will be extended
     * @param other Pointer to the source object (either a list or a tuple) whose elements will be appended
     *
     * @return true if the extension operation was successful, false otherwise
     */
    bool ListExtend(List *list, OObject *other);

    /**
     * @brief Extend a list by appending multiple objects from another array.
     *
     * @param list Pointer to the List object to extend.
     * @param other Array of objects to append to the list.
     * @param count Number of objects in the 'other' array to append.
     * @return True on success. If the operation fails, false will be returned, and the list state remains unchanged.
     */
    bool ListExtend(List *list, OObject **other, MSize count);

    /**
    * @brief Insert element into the list.
    *
    * @param list List object.
    * @param object Object to insert.
    * @param index Location to insert the object.
    * @return True on success, in case of error false will be returned and the panic state will be set.
    */
    bool ListInsert(List *list, OObject *object, MSSize index);

    /**
     * @brief Prepend object to the list.
     *
     * @param list List object.
     * @param object Object to prepend list.
     * @return True on success, in case of error false will be returned and the panic state will be set.
     */
    bool ListPrepend(List *list, OObject *object);

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
    bool ListTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new list instance with the specified capacity
     *
     * This function initializes a new list object in the provided isolate with a given capacity.
     * The list is allocated and configured to hold an array of objects, with all metadata initialized.
     * If the specified capacity is greater than 0, memory allocation for the object's array is performed.
     *
     * @param isolate Pointer to the Isolate where the list will be created
     * @param capacity Intended capacity of the list, determining the number of entries it can initially hold
     *
     * @return A handle to the newly created list if successful, or an empty handle if creation or memory allocation fails
     */
    HList ListNew(Isolate *isolate, MSize capacity);

    /**
     * @brief Creates a new list with an initial capacity.
     *
     * This function initializes a new list using the provided isolate and sets its
     * capacity to the default initial size.
     *
     * @param isolate Pointer to the Isolate instance used for creating the list.
     *
     * @return A newly created list with the default initial capacity.
     */
    inline HList ListNew(Isolate *isolate) {
        return ListNew(isolate, kListInitialCapacity);
    }

    /**
     * @brief Get element from the list at specified index.
     *
     * @param list List object.
     * @param success Pointer to bool that will be set to true on success, false on failure.
     * @param index Location from which to get the object.
     * @return Retrieved object on success, in case of error empty HOObject will be returned and success will be set to false.
     */
    HOObject ListGet(List *list, bool *success, MSSize index);

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
    HOType ListTypeInit(Isolate *isolate);

    /**
     * @brief Removes an element from a list at the specified index
     *
     * This function deletes an element from the input list at the given index
     * and shifts subsequent elements to maintain the list's continuity.
     * If the index is negative, it is treated as an offset from the end of the list.
     *
     * If the index is out of range (either too large or too small), the function
     * returns without altering the list.
     *
     * @param list Pointer to the List object from which an element is to be removed
     * @param index Index of the element to remove, can be negative to indicate an offset from the end
     */
    void ListRemove(List *list, MSSize index);
}

#endif // !ORBIT_ORBITER_DATATYPE_LIST_H_
