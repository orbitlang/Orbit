// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MODULE_NET_H_
#define ORBIT_ORBITER_MODULE_NET_H_

#include <gyro/tcp.h>

#ifdef _ORBIT_PLATFORM_WINDOWS
#else
#include <sys/socket.h>
#endif

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::module {
    struct Sockaddr {
        OROBJ_HEAD;

        sockaddr_storage addr;
        socklen_t length;
    };

    struct TCPHandle {
        OROBJ_HEAD;

        gyro_tcp_t *handle;
    };

    /**
     * @brief Map an address-family atom to its AF_* constant.
     *
     * @param isolate    Owning isolate, used to raise the error.
     * @param atom       The atom name without its `@`: INET, INET6 or, off
     *                   Windows, UNIX.
     * @param out_family Receives the AF_* constant on success.
     *
     * @return true on success; false with a ValueError set when the atom names
     *         no family this platform supports.
     */
    bool SocketAtomToInetFamily(Isolate *isolate, const char *atom, int *out_family);

    /**
     * @brief Map an address-family atom to its AF_* constant.
     *
     * Convenience overload for an atom straight out of the argument list: it
     * reads the atom's interned name and forwards to the overload above.
     *
     * @param isolate    Owning isolate, used to raise the error.
     * @param atom       The atom naming the family: @INET, @INET6 or, off
     *                   Windows, @UNIX.
     * @param out_family Receives the AF_* constant on success.
     *
     * @return true on success; false with a ValueError set when the atom names
     *         no family this platform supports.
     */
    inline bool SocketAtomToInetFamily(Isolate *isolate, const datatype::Atom *atom, int *out_family) {
        return SocketAtomToInetFamily(isolate, ORSTRING_TO_CSTR(atom->id), out_family);
    }

    /**
     * @brief Map an AF_* constant back to its address-family atom.
     *
     * The inverse of SocketAtomToInetFamily: it names a family that came from
     * the system (the `ss_family` of an address, for instance) the way Orbit
     * code spells it.
     *
     * @param isolate Owning isolate, used to intern the atom and to raise the
     *                error.
     * @param family  The AF_* constant to name.
     *
     * @return The atom for that family, or an empty handle with a ValueError
     *         set when the constant names no family this platform supports.
     */
    datatype::HAtom SocketInetFamilyToAtom(Isolate *isolate, int family);

#ifndef _ORBIT_PLATFORM_WINDOWS
    /**
     * @brief Render the path of an AF_UNIX address the way unix(7) classifies it.
     *
     * Only the first `length - offsetof(sun_path)` bytes of sun_path are part of
     * the address, and they are not guaranteed to be NUL-terminated:
     *   - unnamed:  no path bytes at all (or an empty path) -> the empty string;
     *   - abstract: leading NUL (Linux only) -> `@name`, with every NUL of the
     *     name shown as '@', the convention of ss(8) and netstat(8);
     *   - pathname: the path, up to its first NUL.
     *
     * @param isolate Owning isolate, used to build the string.
     * @param s       The Sockaddr holding the AF_UNIX address.
     *
     * @return The rendered path, empty for an unnamed socket, or an empty
     *         handle when the allocation fails.
     */
    datatype::HORString SockaddrUnixPath(Isolate *isolate, const Sockaddr *s);

    /**
     * @brief Render an AF_UNIX address in the display form of the Sockaddr type.
     *
     * Wraps the path produced by SockaddrUnixPath, naming an address with no
     * path `unnamed`.
     *
     * @param isolate Owning isolate, used to format the string.
     * @param s       The Sockaddr holding the AF_UNIX address.
     *
     * @return A String object representing the formatted address.
     */
    datatype::OObject *SockaddrUnixToString(Isolate *isolate, const Sockaddr *s);
#endif


    /**
     * @brief Raise the OSError matching a getaddrinfo/getnameinfo failure.
     *
     * The EAI_* codes are a namespace of their own, not errno values: EAI_SYSTEM
     * defers to errno, EAI_MEMORY maps onto NO_MEMORY, and everything else is
     * reported as OTHER with its gai_strerror text.
     *
     * @param isolate Owning isolate, used to raise the error.
     * @param code    The EAI_* error code from getaddrinfo/getnameinfo.
     * @param context Context string identifying the failing operation.
     */
    void SocketSetGaiError(Isolate *isolate, int code, const char *context);
}

#endif
