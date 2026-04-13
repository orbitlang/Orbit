// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_DICT_H_
#define ORBIT_ORBITER_DATATYPE_DICT_H_

#include <orbit/orbiter/sync/asyncrwlock.h>

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/hashmap.h>

namespace orbiter::datatype {
    struct Dict {
        OROBJ_HEAD;

        ORHMap dict;

        sync::AsyncRWLock lock;
    };

    using HDict = Handle<Dict>;

    /**
     * @brief Inserts a key-value pair into the dictionary
     *
     * This function attempts to insert a key-value pair into the specified dictionary.
     * If the key already exists in the dictionary, its associated value is updated
     * with the provided value. If the key does not exist, a new entry is created and
     * added to the dictionary.
     *
     * @param dict Pointer to the dictionary object where the key-value pair will be inserted
     * @param key Pointer to the key object to be inserted or updated
     * @param value Pointer to the value object to be associated with the given key
     *
     * @return true if the key-value pair was successfully inserted or updated, false otherwise
     */
    bool DictInsert(Dict *dict, OObject *key, OObject *value);

    /**
     * @brief Inserts a key-value pair into the dictionary
     *
     * This function adds an entry to the given dictionary, associating the specified
     * key with its corresponding value. If the key already exists in the dictionary,
     * its value is updated with the new value provided.
     *
     * @param dict Pointer to the dictionary where the key-value pair will be inserted
     * @param key C-string representing the key that uniquely identifies the value
     * @param value Pointer to the value to be associated with the given key
     *
     * @return true if insertion or update was successful, false otherwise
     */
    bool DictInsert(Dict *dict, const char *key, OObject *value);

    /**
     * @brief Looks up a value in the dictionary based on the given key
     *
     * This function searches the specified dictionary for the provided key
     * and retrieves the associated value if the key is found. If the key exists
     * in the dictionary, the corresponding value is stored in the output parameter.
     *
     * @param dict Pointer to the dictionary object to search
     * @param key Pointer to the key object to look for in the dictionary
     * @param out_value Reference to a handle where the associated value will be stored if the key is found
     *
     * @return true if the key was found and the value was successfully retrieved, false otherwise
     */
    bool DictLookup(Dict *dict, OObject *key, HOObject &out_value);

    /**
     * @brief Looks up a value in the dictionary by its key
     *
     * This function searches the specified dictionary for an entry with the given key.
     * If the key is found, the associated value is stored in the provided output parameter.
     * If the key does not exist in the dictionary, the function fails.
     *
     * @param dict Pointer to the dictionary object to search
     * @param key Pointer to a null-terminated string representing the key to locate
     * @param out_value Reference to an HOObject where the found value will be stored, if the key exists
     *
     * @return true if the key was successfully found and the value retrieved, false otherwise
     */
    bool DictLookup(Dict *dict, const char *key, HOObject &out_value);

    /**
     * @brief Removes a key-value pair from the dictionary
     *
     * This function removes a key and its associated value from the specified dictionary.
     * If the key exists, its entry is removed and the associated resources are released.
     * If the key does not exist, no changes are made to the dictionary.
     *
     * @param dict Pointer to the dictionary object from which the key-value pair will be removed
     * @param key Pointer to the key object to be removed
     *
     * @return true if the key-value pair was successfully removed, false if the key was not found
     */
    bool DictRemove(Dict *dict, OObject *key);

    /**
     * @brief Removes a key-value pair from the dictionary
     *
     * This function removes the entry associated with the specified key from the
     * given dictionary. If the key exists in the dictionary, the entry is deleted,
     * and any associated resources are released. If the key does not exist, no
     * modifications are made to the dictionary.
     *
     * @param dict Pointer to the dictionary object from which the key-value pair will be removed
     * @param key Pointer to the key identifying the entry to be removed
     *
     * @return true if the key was found and successfully removed, false otherwise
     */
    bool DictRemove(Dict *dict, const char *key);

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
    bool DictTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new dictionary object with an optional initial capacity
     *
     * This function creates and initializes a new dictionary object within the specified isolate.
     * If the `size` parameter is greater than zero, the dictionary will attempt to allocate space
     * for the specified number of elements. Otherwise, it will use a default initialization strategy.
     *
     * @param isolate Pointer to the isolate context in which this dictionary will be created
     * @param size The initial number of elements for which the dictionary should allocate space, or 0 for default
     *
     * @return A handle to the newly created dictionary object, or an empty handle if the creation fails
     */
    HDict DictNew(Isolate *isolate, U32 size);

    inline HDict DictNew(Isolate *isolate) {
        return DictNew(isolate, 0);
    }

    /**
     * @brief Creates a new dictionary object based on the provided object
     *
     * This function creates a new dictionary that copies the contents of an existing dictionary
     * if the input object is of dictionary type. If the object is not compatible or unsupported,
     * the function returns an empty handle and sets the appropriate error.
     *
     * @param object Pointer to the source object. It must be of a supported type (e.g., dictionary, list, ...).
     *
     * @return A handle to the newly created dictionary if successful. Returns an empty handle ({}) if the object is not compatible.
     */
    HDict DictNew(OObject *object);

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
    HOType DictTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_DICT_H_
