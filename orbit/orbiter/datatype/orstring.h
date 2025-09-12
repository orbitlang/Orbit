// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OSTRING_H_
#define ORBIT_ORBITER_DATATYPE_OSTRING_H_

#include <cstring>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/oobject.h>

#define ORSTRING_TO_CSTR(string)     ((string)->buffer)
#define ORSTRING_LENGTH(string)      ((string)->length)

namespace orbiter::datatype {
    enum class StringKind {
        ASCII,
        UTF8_2,
        UTF8_3,
        UTF8_4
    };

    struct ORString {
        OROBJ_HEAD;

        /* Raw buffer */
        unsigned char *buffer;

        /* String mode */
        StringKind kind;

        /* Interned string */
        bool intern;

        /* Length in bytes */
        MSize length;

        /* Number of graphemes in string */
        MSize cp_length;

        /* String hash */
        MSize hash;
    };

    using HORString = Handle<ORString>;

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
    bool ORStringTypeSetup(TypeInfo *self);

    /**
     * @brief Compares two strings lexicographically.
     *
     * @param left Left Orbit string.
     * @param right Right Orbit string.
     * @return An int value:
     * 0 if the string is equal to the other string.
     * < 0 if the string is lexicographically less than the other string.
     * > 0 if the string is lexicographically greater than the other string (more characters).
     */
    int ORStringCompare(const ORString *left, const ORString *right);

    /**
    * @brief Compares two strings lexicographically.
    *
    * @param left Left Orbit string.
    * @param right Right C-string.
    * @return An int value:
    * 0 if the string is equal to the other string.
    * < 0 if the string is lexicographically less than the other string.
    * > 0 if the string is lexicographically greater than the other string (more characters).
    */
    int ORStringCompare(const ORString *left, const char *right, MSize length);

    /**
    * @brief Compares two strings lexicographically.
    *
    * @param left Left C-string.
    * @param right Right Orbit string.
    * @return An int value:
    * 0 if the string is equal to the other string.
    * < 0 if the string is lexicographically less than the other string.
    * > 0 if the string is lexicographically greater than the other string (more characters).
    */
    int ORStringCompare(const char *left, const ORString *right, MSize length);

    /**
     * @brief Compare an ORString instance with a C-style string
     *
     * This function compares the given ORString object with the provided
     * null-terminated C-style string. The comparison checks for equality
     * and handles the required length calculation for the right-hand string.
     *
     * @param left Pointer to the ORString object
     * @param right Pointer to the null-terminated C-style string
     *
     * @return An integer indicating the result of the comparison:
     *         - 0 if the strings are equal
     *         - Negative value if the left string is less than the right
     *         - Positive value if the left string is greater than the right
     */
    inline int ORStringCompare(const ORString *left, const char *right) {
        return ORStringCompare(left, right, strlen(right));
    }

    /**
     * @brief Compare an ORString instance with a C-style string
     *
     * This function compares the given ORString object with the provided
     * null-terminated C-style string. The comparison checks for equality
     * and handles the required length calculation for the right-hand string.
     *
     * @param left Pointer to the null-terminated C-style string
     * @param right Pointer to the ORString object
     *
     * @return An integer indicating the result of the comparison:
     *         - 0 if the strings are equal
     *         - Negative value if the left string is less than the right
     *         - Positive value if the left string is greater than the right
     */
    inline int ORStringCompare(const char *left, const ORString *right) {
        return ORStringCompare(left, right, strlen(left));
    }

    /**
     * @brief Compares two ORString objects for equality
     *
     * This function determines whether two ORString objects are equal
     * by comparing their contents using the ORStringCompare function.
     *
     * @param left Pointer to the first ORString object.
     * @param right Pointer to the second ORString object.
     *
     * @return true if the strings are equal, false otherwise.
     */
    inline bool ORStringEqual(const ORString *left, const ORString *right) {
        return ORStringCompare(left, right) == 0;
    }

    /**
     * @brief Formats a string with specified arguments and returns it as an ORString handle
     *
     *
     * @param isolate Pointer to the Isolate instance associated with this operation
     * @param format The format string that defines how the output string is constructed
     * @param ... Variadic arguments to be formatted according to the format string
     *
     * @return A handle to an ORString containing the formatted string. If the operation fails,
     *         an empty handle is returned.
     */
    HORString ORStringFormat(Isolate *isolate, const char *format, ...);

    /**
     * @brief Formats a string with variable arguments and returns an ORString handle
     *
     * @param isolate Pointer to the Isolate in which the string will be created and tracked
     * @param format A C-style format string specifying the formatting options
     * @param args A va_list of arguments to be formatted based on the format string
     *
     * @return A handle to an ORString containing the formatted string. If the operation fails,
     *         an empty handle is returned.
     */
    HORString ORStringFormat(Isolate *isolate, const char *format, va_list args);

    /**
     * @brief Creates an exact copy of a String object in the String pool and return it
     *
     * @param isolate Pointer to the Isolate
     * @param string The C-string to convert to Orbit string
     * @param length The length of the C-string
     *
     * @return Handle to the interned string
     */
    HORString ORStringIntern(Isolate *isolate, const unsigned char *string, MSize length);

    /**
     * @brief Creates an exact copy of a String object in the String pool and return it
     *
     * @param isolate Pointer to the Isolate
     * @param string The C-string to convert to Orbit string
     *
     * @return Handle to the interned string
     */
    inline HORString ORStringIntern(Isolate *isolate, const char *string) {
        return ORStringIntern(isolate, (const unsigned char *) string, strlen(string));
    }

    /**
     * @brief Create new string
     *
     * It allows you to build an empty String object (container only) which must subsequently be filled by the applicant
     *
     * @warning: Buffer must be zero terminated, the value of length MUST NOT include the terminator character,
     * so the buffer must pass the following assertion: buffer[length] == '\0'.
     * Obviously the size of the allocated buffer must be sufficient to also contain the terminator character
     *
     * @param isolate Pointer to the Isolate
     * @param string Raw buffer containing the string (ownership of the buffer will be transferred to the created object)
     * @param length Length of the buffer
     * @param cp_length Number of unicode code point in the buffer
     * @param kind StringKind
     *
     * @return Handle to ORString object
     */
    HORString ORStringNew(Isolate *isolate, unsigned char *string, MSize length, MSize cp_length,
                          StringKind kind);

    /**
     * @brief Create new string
     *
     * @param isolate Pointer to the Isolate
     * @param string The unsigned C-string to convert to Orbit string
     * @param length The length of the C-string
     *
     * @return Handle to ORString object
     */
    HORString ORStringNew(Isolate *isolate, const unsigned char *string, MSize length);

    /**
     * @brief Create new string
     *
     * @param isolate Pointer to the Isolate
     * @param string The C-string to convert to Orbit string
     * @param length The length of the unsigned C-string
     *
     * @return Handle to ORString object
     */
    inline HORString ORStringNew(Isolate *isolate, const char *string, MSize length) {
        return ORStringNew(isolate, (const unsigned char *) string, length);
    }

    /**
     * @brief Create new string
     *
     * @param isolate Pointer to the Isolate
     * @param string The C-string to convert to Orbit string
     *
     * @return Handle to ORString object
     */
    inline HORString ORStringNew(Isolate *isolate, const char *string) {
        return ORStringNew(isolate, (unsigned char *) string, strlen(string));
    }

    /**
     * @brief Create a new string object using the buffer parameter as an internal buffer
     *
     * The new string object becomes the owner of the buffer passed as a parameter
     *
     * @warning: Buffer must be zero terminated, the value of length MUST NOT include the terminator character,
     * so the buffer must pass the following assertion: buffer[length] == '\0'.
     * Obviously the size of the allocated buffer must be sufficient to also contain the terminator character
     *
     * @param isolate Pointer to the Isolate
     * @param buffer Raw buffer containing the string
     * (ownership of the buffer will be transferred to the created object).
     * @param length Length of the buffer.
     *
     * @return Handle to ORString object
     */
    HORString ORStringNewHoldBuffer(Isolate *isolate, unsigned char *buffer, MSize length);

    MSize ORStringHash(ORString *string);

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
    HOType ORStringTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_OSTRING_H_
