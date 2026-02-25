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
#include <Windows.h>
#include <Psapi.h>

static FARPROC FindProcInDefaultScope(const char *sym_name) {
    FARPROC proc = nullptr;

    if (sym_name == nullptr || sym_name[0] == '\0')
        return nullptr;

    // 1) Try in the main module (EXE).
    HMODULE exe = GetModuleHandleW(nullptr);
    if (exe) {
        if ((proc = GetProcAddress(exe, sym_name)) != nullptr)
            return proc;
    }

    // 2) Enumerate loaded modules in the process and try GetProcAddress on each one.
    HMODULE mods[1024];
    DWORD needed = 0;
    const HANDLE self = GetCurrentProcess();

    if (!EnumProcessModules(self, mods, sizeof(mods), &needed))
        return nullptr;

    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i) {
        if (mods[i] == nullptr)
            continue;

        if ((proc = GetProcAddress(mods[i], sym_name)) != nullptr)
            return proc;
    }

    return nullptr;
}

DLHandle orbiter::native::OpenLibrary(Isolate *isolate, const char *path) {
    if (path == nullptr)
        return kDefaultScopeHandle;

    const auto handle = LoadLibraryEx(path, nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NATIVE_LOAD_LIBRARY],
                 path,
                 ErrorGetMsgFromWinErr(isolate).get()
        );

        return DLHandleError;
    }

    return handle;
}

DLHandle orbiter::native::LoadSymbol(Isolate *isolate, const DLHandle handle, const char *sym_name) {
    DLHandle symbol;

    if (handle == kDefaultScopeHandle)
        symbol = (DLHandle) FindProcInDefaultScope(sym_name);
    else
        symbol = (DLHandle) GetProcAddress(static_cast<HMODULE>(handle), sym_name);

    if (symbol == nullptr) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NATIVE_LOAD_SYMBOL],
                 sym_name,
                 ErrorGetMsgFromWinErr(isolate).get()
        );

        return DLHandleError;
    }

    return symbol;
}

int orbiter::native::CloseLibrary(Isolate *isolate, DLHandle handle, const char *lib_name) {
    if (handle == kDefaultScopeHandle || handle == DLHandleError)
        return 0;

    const BOOL ok = FreeLibrary(static_cast<HMODULE>(handle));
    if (!ok) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NATIVE_UNLOAD_LIBRARY],
                 lib_name == nullptr ? "" : lib_name,
                 ErrorGetMsgFromWinErr(isolate).get()
        );

        return -1;
    }

    return 0;
}

#else
DLHandle orbiter::native::OpenLibrary(Isolate *isolate, const char *path) {
    DLHandle handle;

    if (path == nullptr)
        return kDefaultScopeHandle;

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
