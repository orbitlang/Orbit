// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#ifdef _ORBIT_PLATFORM_WINDOWS

#include <Windows.h>

#undef ERROR

#include <orbit/orbiter/datatype/error.h>

using namespace orbiter::datatype;

HORString orbiter::datatype::ErrorGetMsgFromWinErr(Isolate *isolate) {
    HORString msg;

    LPSTR mbuffer = nullptr;
    DWORD eid;

    if ((eid = ::GetLastError()) != 0) {
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                     FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS |
                                     FORMAT_MESSAGE_MAX_WIDTH_MASK,
                                     nullptr,
                                     eid,
                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     (LPSTR) &mbuffer,
                                     0,
                                     nullptr);

        if (size == 0)
            msg = ORStringNew(isolate, "FormatMessageA failed. Could not get error message from Windows");
        else
            msg = ORStringNew(isolate, mbuffer, size);

        LocalFree(mbuffer);
    } else
        msg = ORStringNew(isolate, "operation completed successfully");

    return msg;
}

#endif