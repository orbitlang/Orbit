// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_DICT_H_
#define ORBIT_ORBITER_DATATYPE_DICT_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/hashmap.h>

namespace orbiter::datatype {
    struct Dict {
        OROBJ_HEAD;

        // TODO: sync?!

        ORHMap dict;
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
    bool DictLookup(const Dict *dict, OObject *key, HOObject &out_value);

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
    bool DictLookup(const Dict *dict, const char *key, HOObject &out_value);

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

    HDict DictNew(Isolate *isolate, U32 size);

    inline HDict DictNew(Isolate *isolate) {
        return DictNew(isolate, 0);
    }

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
