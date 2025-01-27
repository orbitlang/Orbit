// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_LIST_H_
#define ORBIT_ORBITER_DATATYPE_LIST_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    constexpr int kListInitialCapacity = 24;

    struct List {
        OROBJ_HEAD;

        // TODO: sync?!

        OObject **objects;

        MSize capacity;

        MSize length;
    };

    using HList = Handle<List>;

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
    bool ListAppend(List *list, const List *other);

    /**
    * @brief Insert element into the list.
    *
    * @param list List object.
    * @param object Object to insert.
    * @param index Location to insert the object.
    * @return True on success, in case of error false will be returned and the panic state will be set.
    */
    bool ListInsert(List *list, OObject *object, MSize index);

    /**
     * @brief Prepend object to the list.
     *
     * @param list List object.
     * @param object Object to prepend list.
     * @return True on success, in case of error false will be returned and the panic state will be set.
     */
    bool ListPrepend(List *list, OObject *object);

    /**
     * @brief Get element from the list at specified index.
     *
     * @param list List object.
     * @param success Pointer to bool that will be set to true on success, false on failure.
     * @param index Location from which to get the object.
     * @return Retrieved object on success, in case of error empty HOObject will be returned and success will be set to false.
     */
    HOObject ListGet(List *list, bool *success, MSize index);

    HList ListNew(Isolate *isolate, MSize capacity);

    inline HList ListNew(Isolate *isolate) {
        return ListNew(isolate, kListInitialCapacity);
    }

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Pointer to the newly created TypeInfo for the type, or nullptr if creation failed
     */
    TypeInfo *ListTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_LIST_H_
