// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#ifdef _ORBIT_PLATFORM_WINDOWS
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <cassert>
#include <cstring>

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/byteview.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/module/modules.h>
#include <orbit/orbiter/module/socket.h>

#include "orbit/orbiter/datatype/number.h"

using namespace orbiter::datatype;
using namespace orbiter::module;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

struct Sockaddr {
    OROBJ_HEAD;

    sockaddr_storage addr;
    socklen_t length;
};

bool orbiter::module::SocketAtomToInetFamily(Isolate *isolate, const char *atom, int *out_family) {
    if (strcmp(atom, "INET") == 0) {
        *out_family = AF_INET;

        return true;
    }
    if (strcmp(atom, "INET6") == 0) {
        *out_family = AF_INET6;

        return true;
    }

#ifndef _ORBIT_PLATFORM_WINDOWS
    if (strcmp(atom, "UNIX") == 0) {
        *out_family = AF_UNIX;

        return true;
    }
#endif

    ErrorSet(isolate,
             ValueError::Details[ValueError::ID],
             nullptr,
#ifdef _ORBIT_PLATFORM_WINDOWS
             "unknown address family '@%s', expected @INET or @INET6",
#else
             "unknown address family '@%s', expected @INET, @INET6 or @UNIX",
#endif
             atom);

    return false;
}

// *********************************************************************************************************************
// EXPORTED FUNCTIONS
// *********************************************************************************************************************

RUNTIME_FUNCTION(socket_parse, parse,
                 R"DOC(
@brief Build a socket address from a literal IP address and a port.

Parses `host` as a numeric address of `family`: no name resolution is performed
and nothing is sent over the network, so the call never blocks. A hostname is
rejected rather than looked up.

@param host          Address in its textual form, as bytes or a string: dotted
                     quad for @INET, colon-separated for @INET6.
@param port          Port number, 0 to 65535. 0 asks the system to pick one
                     when the address is later bound.
@param family=@INET  Address family the host is written in: @INET or @INET6.

@return A new Sockaddr for that endpoint.

@panic OOMError   When memory allocation fails.
@panic TypeError  When a parameter has an invalid type.
@panic ValueError When `family` names no known address family, when it is one
                  that has no literal form (@UNIX), when `port` is out of
                  range, or when `host` is not a valid address of that family.

@example
    parse("127.0.0.1", 8080)
    parse("::1", 8080, family=@INET6)
    parse("0.0.0.0", 0)             // every interface, any port
    parse("example.org", 80)        // ValueError: not a literal address
)DOC", 2, "family", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("host", false, InstanceType::BYTES, InstanceType::STRING),
                   PCHECK_DEF("port", false, InstanceType::NUMBER),
                   PCHECK_DEF("family", true, InstanceType::ATOM));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    const auto prop = TIFindLocalProperty(O_GET_TYPE(_func->shared->module), "Sockaddr");
    assert(prop != nullptr);

    int family = AF_INET;
    if (!O_IS_SENTINEL(argv[2])) {
        if (!SocketAtomToInetFamily(isolate, (Atom *) argv[2], &family))
            return {};
    }

    // inet_pton only knows the internet families
    if (family != AF_INET && family != AF_INET6) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "'%s' does not parse an address of this family",
                 "parse");

        return {};
    }

    IntegerUnderlying port;
    if (!NumberExtract(argv[1], port))
        return {};

    if (port < 0 || port > 65535) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "port must be in 0..65535, got %lld",
                 (long long) port);

        return {};
    }

    const ByteView view(isolate, argv[0]);
    if (!view.Ok())
        return {};

    char host[INET6_ADDRSTRLEN]{};
    if (view.Size() >= sizeof(host)) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "invalid address: %u bytes is too long for a literal address",
                 (unsigned) view.Size());

        return {};
    }

    orbiter::memory::MemoryCopy(host, view.Data(), view.Size());

    sockaddr_storage storage{};
    socklen_t length;
    int ok;

    if (family == AF_INET) {
        auto *in = (sockaddr_in *) &storage;

        in->sin_family = AF_INET;
        in->sin_port = htons((unsigned short) port);

        length = sizeof(sockaddr_in);

        ok = inet_pton(AF_INET, host, &in->sin_addr);
    } else {
        auto *in6 = (sockaddr_in6 *) &storage;

        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons((unsigned short) port);

        length = sizeof(sockaddr_in6);

        ok = inet_pton(AF_INET6, host, &in6->sin6_addr);
    }

    // The 4.4BSD stacks (Darwin, FreeBSD, NetBSD, OpenBSD) carry the length in
    // the address itself; Linux, Windows and Solaris have no such field. Test
    // SIN6_LEN is the macro RFC 2553 defines for exactly this question,
    // and only those stacks define it.
#ifdef SIN6_LEN
    ((sockaddr *) &storage)->sa_len = (unsigned char) length;
#endif

    if (ok == 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "invalid %s address: '%s'",
                 family == AF_INET ? "IPv4" : "IPv6",
                 host);

        return {};
    }

    if (ok < 0) {
        // Unreachable: the only error POSIX defines here is EAFNOSUPPORT, and the
        // family is already one of the two supported ones. Reported anyway, since
        // the platforms word it more loosely.

        ErrorSetFromErrno(isolate, host);

        return {};
    }

    auto *saddr = MakeObject<Sockaddr>((TypeInfo *) prop->value, 0);
    if (saddr == nullptr)
        return {};

    saddr->addr = storage;
    saddr->length = length;

    O_GC_TRACK_RETURN(isolate, (OObject *) saddr, false);
}

constexpr FunctionDef sockaddr_methods[] = {
    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

constexpr ModuleEntry socket_entries[] = {
    ORBIT_MODULE_EXPORT_ALIAS("Sockaddr", nullptr),
    ORBIT_MODULE_EXPORT_FUNCTION(socket_parse),

    ORBIT_MODULE_SENTINEL
};

static bool ModuleSocketInit(Module *self) {
    auto *isolate = O_GET_ISOLATE(self);
    const auto *type = O_GET_TYPE(self);

    const auto tp_handle = MakeType(isolate, "Sockaddr", InstanceType::OBJECT,
                                    sizeof(Sockaddr) - sizeof(OObject), 1,
                                    0);

    if (!ModuleSetLocalProperty(type, nullptr, (OObject *) tp_handle.get()))
        return false;

    if (!TIPropertyAdd(tp_handle.get(), sockaddr_methods, PropertyFlag::IS_PUBLIC))
        return false;

    return true;
}

ModuleInit ModuleSocket = {
    "::orbit::net::socket",
    "@brief Socket addresses."
    "\n\n"
    "Builds the addresses the socket modules take and return. An address is an "
    "endpoint, the host together with its port, kept in the form the operating "
    "system expects so that nothing is lost in translation.",
    "1.0.0",
    socket_entries,
    ModuleSocketInit,
    nullptr
};

const ModuleInit *orbiter::module::module_net_socket_ = &ModuleSocket;
