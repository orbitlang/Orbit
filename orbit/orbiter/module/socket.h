// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MODULE_SOCKET_H_
#define ORBIT_ORBITER_MODULE_SOCKET_H_

#include <orbit/orbiter/datatype/atom.h>

namespace orbiter::module {
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
}

#endif
