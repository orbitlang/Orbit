// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OSTRING_H_
#define ORBIT_ORBITER_DATATYPE_OSTRING_H_

#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/oobject.h>

#define ORSTRING_TO_CSTR(string)     ((const char *)((string)->buffer))
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
     * @brief Test whether @p self contains @p sub as a substring.
     *
     * Performs a non-overlapping exact byte match — the empty @p sub is always
     * considered present.
     *
     * @param self Haystack string.
     * @param sub  Needle string.
     *
     * @return true if @p sub occurs anywhere in @p self, false otherwise.
     */
    bool ORStringContains(const ORString *self, const ORString *sub);

    /**
     * @brief Checks if a string ends with the specified suffix
     *
     * This function determines whether the given string ends with the provided suffix.
     *
     * @param self Pointer to the ORString instance to check
     * @param suffix Pointer to the ORString instance representing the suffix to match
     *
     * @return true if the string ends with the specified suffix, false otherwise
     */
    bool ORStringEndsWith(const ORString *self, const ORString *suffix) noexcept;

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
     * @brief Creates a new ORString instance from a StringBuilder object
     *
     * This function constructs an ORString object using the data provided by the StringBuilder.
     * It extracts the string buffer, its length, code point count, and string kind from the builder,
     * then creates and returns an ORString instance with the extracted data. If the StringBuilder
     * fails to generate a valid buffer or the ORString creation fails, an empty handle is returned.
     * The function ensures proper resource management by releasing resources associated
     * with the StringBuilder after the ORString is successfully created.
     *
     * @param isolate Pointer to the Isolate
     * @param builder Reference to a StringBuilder object containing the string data to be used
     *
     * @return A handle to the newly created ORString object, or an empty handle if creation fails
     */
    HORString ORStringNew(Isolate *isolate, class StringBuilder &builder);

    /**
     * @brief Create a new string object using the buffer parameter as an internal buffer
     *
     * Ownership of the buffer is transferred to the new string object **only if the function
     * succeeds** (i.e. returns a non-empty handle). If the function fails, the caller retains
     * ownership of the buffer and is responsible for releasing it.
     *
     * @warning: Buffer must be zero terminated, the value of length MUST NOT include the terminator
     * character, so the buffer must pass the following assertion: buffer[length] == '\0'.
     * Obviously the size of the allocated buffer must be sufficient to also contain the terminator
     * character.
     *
     * @param isolate Pointer to the Isolate
     * @param buffer Raw buffer containing the string. Ownership is transferred on success.
     * @param length Length of the buffer, not including the null terminator.
     *
     * @return Handle to the new ORString object, or an empty handle on failure.
     *         On failure the caller still owns @p buffer.
     */
    HORString ORStringNewHoldBuffer(Isolate *isolate, unsigned char *buffer, MSize length);

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

    /**
     * @brief Compute the hash value for the given ORString object
     *
     * This function calculates the hash value for an ORString object if it has not
     * been computed already. If the hash value is already cached, it simply returns
     * the cached value. Otherwise, it computes the hash using the `StrHash` function
     * and caches the result in the `hash` field of the ORString object.
     *
     * @param string Pointer to the ORString object for which the hash is to be computed
     *
     * @return The computed or cached hash value as an MSize type
     */
    MSize ORStringHash(ORString *string) noexcept;

    /**
     * @brief Perform a reverse search for a substring within a string
     *
     * This function searches for the last occurrence of a given substring
     * within the specified string. The search is performed in reverse, starting
     * from the end of the string and moving toward the beginning. If the substring
     * is found, its starting index is returned; otherwise, a value indicating
     * failure is returned.
     *
     * @param self Pointer to the ORString instance in which to search
     * @param sub Pointer to the ORString instance representing the substring to find
     *
     * @return The index of the last occurrence of the substring within the ORString.
     *         If the substring is not found, the function returns -1.
     */
    MSSize ORStringRFind(const ORString *self, const ORString *sub) noexcept;

    /**
     * @brief Find the last occurrence of a substring within an ORString
     *
     * This function searches for the last occurrence of the specified substring
     * within the given ORString instance. The search is performed in reverse order,
     * starting from the end of the string.
     *
     * @param self Pointer to the ORString instance in which to search
     * @param sub Pointer to the null-terminated C-string representing the substring to find
     *
     * @return The index of the last occurrence of the substring within the ORString.
     *         If the substring is not found, the function returns -1.
     */
    MSSize ORStringRFind(const ORString *self, const char *sub) noexcept;

    /**
     * @brief Split a string on a separator and return a list of substrings.
     *
     * Performs exact, non-overlapping byte matching against @p sep. Empty
     * leading, trailing and interior segments are preserved — splitting
     * `"aaaa"` by `"aa"` yields three empty strings.
     *
     * The caller is responsible for ensuring @p sep is non-empty (the
     * underlying primitive asserts that condition); on Orbit-method paths
     * this is enforced by raising a `ValueError` before invoking this
     * function.
     *
     * @param isolate  Owning isolate; used for allocation and refcount tracking
     *                 of both the returned list and each substring.
     * @param self     Source string to split.
     * @param sep      Non-empty separator string.
     * @param maxsplit Maximum number of splits to perform. The resulting list
     *                 contains at most @p maxsplit + 1 elements. Negative
     *                 means unbounded — every separator occurrence is split.
     *
     * @return Handle to a new List of ORString instances. Empty handle on
     *         allocation failure.
     */
    HList ORStringSplit(Isolate *isolate, const ORString *self, const ORString *sep, MSSize maxsplit = -1);

    /**
     * @brief Split a string on runs of ASCII whitespace and return a list of substrings.
     *
     * Empty leading, trailing and interior segments are dropped — `"  a  b  "`
     * yields `["a", "b"]`. ASCII whitespace is space, tab, newline, vertical
     * tab, form feed and carriage return.
     *
     * @param isolate  Owning isolate; used for allocation and refcount tracking
     *                 of both the returned list and each substring.
     * @param self     Source string to split.
     * @param maxsplit Maximum number of splits to perform. After the limit is
     *                 reached, the remainder of the string (with only its
     *                 leading whitespace stripped) becomes the final element.
     *                 Negative means unbounded.
     *
     * @return Handle to a new List of ORString instances. Empty handle on
     *         allocation failure.
     */
    HList ORStringSplitWhitespace(Isolate *isolate, const ORString *self, MSSize maxsplit = -1);
}

#endif // !ORBIT_ORBITER_DATATYPE_OSTRING_H_
