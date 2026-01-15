// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#include <orbit/orbiter/native/dlwrap.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

using namespace orbiter::datatype;
using namespace orbiter::native;

#ifdef _ORBIT_PLATFORM_WINDOWS

DLHandle orbiter::native::OpenLibrary(Isolate *isolate, const char *path) {
}

DLHandle orbiter::native::LoadSymbol(Isolate *isolate, DLHandle handle, const char *sym_name) {
}

int orbiter::native::CloseLibrary(Isolate *isolate, DLHandle handle) {
}

#else
#include <dlfcn.h>

DLHandle orbiter::native::OpenLibrary(Isolate *isolate, const char *path) {
    DLHandle handle;

    if (path == nullptr)
        return RTLD_DEFAULT;

    if ((handle = dlopen(path, RTLD_NOW)) == nullptr) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NATIVE_LOAD_LIBRARY],
                 path,
                 dlerror()
        );

        return DLHandleError;
    }

    return handle;
}

DLHandle orbiter::native::LoadSymbol(Isolate *isolate, const DLHandle handle, const char *sym_name) {
    auto *symbol = dlsym(handle, sym_name);
    if (symbol == nullptr) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NATIVE_LOAD_SYMBOL],
                 sym_name,
                 dlerror()
        );

        return DLHandleError;
    }

    return symbol;
}

int orbiter::native::CloseLibrary(Isolate *isolate, const DLHandle handle, const char *lib_name) {
    const auto error = dlclose(handle);
    if (error != 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NATIVE_UNLOAD_LIBRARY],
                 lib_name == nullptr ? "" : lib_name,
                 dlerror()
        );
    }

    return error;
}
#endif
