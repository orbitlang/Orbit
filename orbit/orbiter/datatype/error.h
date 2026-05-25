// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ERROR_H_
#define ORBIT_ORBITER_DATATYPE_ERROR_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/atom.h>

namespace orbiter::datatype {
    struct Error {
        OROBJ_HEAD;

        Atom *kind;

        ORString *reason;

        OObject *details;
    };

    using HError = Handle<Error>;

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
    bool ErrorTypeSetup(TypeInfo *self);

    /**
     * @brief Create a new error object with specified details
     *
     * @param isolate The isolate within which the error object is created
     * @param kind Specifies the type or classification of the error
     * @param reason A string describing the reason for the error
     * @param details Additional details about the error, which might include context or supplementary data
     *
     * @return A handle to the newly created error object if successful, or an empty handle otherwise
     */
    HError ErrorNew(Isolate *isolate, Atom *kind, ORString *reason, OObject *details);

    /**
     * @brief Creates a new error object with the specified details and formatted reason
     *
     * This function initializes an error object, providing information about the nature
     * of the error, associated details, and a formatted reason message. The reason can
     * include additional arguments that are formatted according to the specified format string.
     *
     * @param isolate Pointer to the Isolate associated with the error creation
     * @param kind A null-terminated string representing the type or category of error
     * @param details Pointer to an OObject containing additional error details
     * @param format A null-terminated format string used to generate the error reason
     * @param ... Variable arguments to be formatted into the error reason
     *
     * @return A handle to the newly created error object if successful, or an empty handle otherwise
     */
    HError ErrorNew(Isolate *isolate, const char *kind, OObject *details, const char *format, ...);

#ifdef _ORBIT_PLATFORM_WINDOWS
    /**
     * @brief Retrieve an error message string corresponding to the last Windows error
     *
     * This function obtains a descriptive error message for the last error code reported
     * by the Windows operating system. It uses the `GetLastError` API to retrieve the
     * error code and `FormatMessageA` to convert it into a human-readable string.
     *
     * The function performs the following tasks:
     * - Checks the last error code using `GetLastError`.
     * - If an error code exists, retrieves the associated error message using `FormatMessageA`.
     * - Allocates a string.
     * - Frees any allocated memory used by `FormatMessageA`.
     * - Returns either the retrieved message or a default message for successful operations.
     *
     * @param isolate Pointer to the `Isolate` instance used for string allocation and error message management
     *
     * @return A handle to an `ORString` containing the error message. If no error occurred, a message indicating
     *         successful execution is returned.
     */
    HORString ErrorGetMsgFromWinErr(Isolate * isolate);
#endif

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
    HOType ErrorTypeInit(Isolate *isolate);

    /**
     * @brief Sets an error for the current execution context
     *
     * This function creates a new error object based on the provided kind, details, and formatted message,
     * and marks the current fiber as panicked using that error object.
     *
     * @param isolate Pointer to the current Isolate object managing the execution context
     * @param kind A string describing the category or type of the error
     * @param details Pointer to an OObject that includes additional context or details about the error
     * @param format A format string for constructing the error message
     * @param ... Additional arguments matching the format string to include in the error message
     */
    void ErrorSet(Isolate *isolate, const char *kind, OObject *details, const char *format, ...);

    /**
     * @brief Sets an error based on the current value of errno
     *
     * Maps the system's errno value into an OSError instance with an appropriate reason
     * and attaches additional details to the error. This function handles common errno
     * values explicitly and falls back to a generic "OTHER" reason for unknown values.
     *
     * @param isolate Pointer to the Isolate instance used to manage the context in which the error is raised
     * @param message Optional context message to include with the error; defaults to an empty string if nullptr
     */
    void ErrorSetFromErrno(Isolate *isolate, const char *message);

    /**
     * @brief Sets an error message with object type information
     *
     * Constructs an error message that incorporates the type information of the specified object.
     * If a primary parameter (`p1`) is provided, it is included in the error message alongside
     * the type information derived from the specified object. Otherwise, only the type information
     * and error details defined by `format` are included.
     *
     * @param isolate Pointer to the Isolate instance used for context management
     * @param kind A string describing the kind of error
     * @param format Format string for the error description
     * @param p1 Optional primary parameter to include in the error message
     * @param target A pointer to the object from which type information will be extracted
     */
    void ErrorSetWithObjType(Isolate *isolate, const char *kind, const char *format, const char *p1, const OObject *target);
}

#endif // !ORBIT_ORBITER_DATATYPE_ERROR_H_
