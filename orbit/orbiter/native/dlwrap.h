// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_NATIVE_DLWRAP_H_
#define ORBIT_ORBITER_NATIVE_DLWRAP_H_

#include <orbit/orbiter/isolate.h>

namespace orbiter::native {
    using DLHandle = void *;

    constexpr DLHandle DLHandleError = nullptr;

    /**
     * @brief Loads a shared library or dynamic-link library at runtime.
     *
     * This function attempts to load the specified library from the given path
     * and returns a handle to the loaded library. If the path is null, it returns
     * a default library handle. In case of failure, it sets an error state
     * within the provided isolate and returns a special error handle.
     *
     * @param isolate A pointer to the current execution context used for error reporting.
     * @param path A string specifying the full path to the library to load, or null to load the default library.
     * @return A handle to the loaded library on success, or an error handle on failure.
     */
    DLHandle OpenLibrary(Isolate *isolate, const char *path);

    /**
     * @brief Loads a symbol from a dynamically loaded library handle.
     *
     * This method looks up the given symbol name within the provided dynamic library handle
     * and returns a pointer to the symbol. If the symbol is not found or an error occurs,
     * it reports the error to the provided isolate and returns an error handle.
     *
     * @param isolate A pointer to the Isolate instance used for error reporting.
     * @param handle The handle to the dynamically loaded library where the symbol resides.
     * @param sym_name The name of the symbol to be loaded.
     * @return A pointer to the loaded symbol if found; otherwise, returns a predefined error handle.
     */
    DLHandle LoadSymbol(Isolate *isolate, DLHandle handle, const char *sym_name);

    /**
     * @brief Closes a dynamically loaded library and handles any errors that occur during the operation.
     *
     * This function unloads the specified shared library represented by the provided handle.
     * If the operation fails, it sets an appropriate error in the given isolate. The error message
     * includes details about the failure and the name of the library, if provided.
     *
     * @param isolate A pointer to the Isolate object. It is used to store error details if the library cannot be closed.
     * @param handle The handle to the dynamically loaded library that needs to be closed.
     * @param lib_name The name of the library to be unloaded. Used for error reporting. Can be nullptr.
     * @return Returns 0 on success. Returns a non-zero value if an error occurs during the closing of the library.
     */
    int CloseLibrary(Isolate *isolate, DLHandle handle, const char *lib_name);
} // namespace orbiter::native

#endif // !ORBIT_ORBITER_NATIVE_DLWRAP_H_
