// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#ifdef _ORBIT_PLATFORM_WINDOWS
#else
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>

#include <orbit/orbiter/evloop.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/byteview.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/module/modules.h>
#include <orbit/orbiter/module/net.h>

using namespace orbiter::datatype;
using namespace orbiter::module;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

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

HAtom orbiter::module::SocketInetFamilyToAtom(Isolate *isolate, const int family) {
    if (family == AF_INET)
        return AtomNew(isolate, "INET");

    if (family == AF_INET6)
        return AtomNew(isolate, "INET6");

#ifndef _ORBIT_PLATFORM_WINDOWS
    if (family == AF_UNIX)
        return AtomNew(isolate, "UNIX");
#endif

    ErrorSet(isolate,
             ValueError::Details[ValueError::ID],
             nullptr,
             "unknown address family '%d'",
             family);

    return {};
}

#ifndef _ORBIT_PLATFORM_WINDOWS
HORString orbiter::module::SockaddrUnixPath(Isolate *isolate, const Sockaddr *s) {
    const auto *un = (const sockaddr_un *) &s->addr;

    constexpr auto path_offset = offsetof(sockaddr_un, sun_path);

    size_t path_len = 0;
    if (s->length > path_offset)
        path_len = std::min<size_t>(s->length - path_offset, sizeof(un->sun_path));

    char path[sizeof(un->sun_path) + 1];
    size_t size = 0;

#ifdef _ORBIT_PLATFORM_LINUX
    if (path_len > 1 && un->sun_path[0] == '\0') {
        path[size++] = '@';

        for (size_t i = 1; i < path_len; i++)
            path[size++] = un->sun_path[i] == '\0' ? '@' : un->sun_path[i];
    } else
#endif
    {
        size = strnlen(un->sun_path, path_len);
        memory::MemoryCopy(path, un->sun_path, size);
    }

    return ORStringNew(isolate, path, size);
}

OObject *orbiter::module::SockaddrUnixToString(Isolate *isolate, const Sockaddr *s) {
    const auto path = SockaddrUnixPath(isolate, s);
    if (!path)
        return nullptr;

    const auto length = ORSTRING_LENGTH(path.get());

    if (length == 0)
        return (OObject *) ORStringFormat(isolate, "%s(unnamed, @UNIX)",
                                          O_GET_TYPE(s)->name).get();

    return (OObject *) ORStringFormat(isolate, "%s(%.*s, @UNIX)",
                                      O_GET_TYPE(s)->name,
                                      (int) length,
                                      ORSTRING_TO_CSTR(path.get())).get();
}
#endif

void orbiter::module::SocketSetGaiError(Isolate *isolate, const int code, const char *context) {
#ifdef EAI_SYSTEM
    if (code == EAI_SYSTEM) {
        ErrorSetFromErrno(isolate, context);

        return;
    }
#endif

    auto *details = (OObject *) O_TO_SMI((MSSize) code);

    if (code == EAI_MEMORY) {
        ErrorSet(isolate,
                 OSError::Details[OSError::ID],
                 details,
                 OSError::Details[OSError::NO_MEMORY],
                 context);

        return;
    }

    ErrorSet(isolate,
             OSError::Details[OSError::ID],
             details,
             OSError::Details[OSError::OTHER],
             code,
             gai_strerror(code),
             context);
}

/// Look up one of the module's exported types by name. The module exports them
/// itself, so a missing entry is a bug in ModuleNetInit rather than a runtime
/// condition.
static TypeInfo *NetExportedType(const Module *module, const char *name) {
    const auto *prop = TIFindLocalProperty(O_GET_TYPE(module), name);

    assert(prop != nullptr);

    return (TypeInfo *) prop->value;
}

/// Check that @p obj really is one of this module's Sockaddr objects before its
/// bytes are handed to the operating system.
static Sockaddr *NetCheckSockaddr(orbiter::Isolate *isolate, const Module *module, OObject *obj) {
    auto *type = NetExportedType(module, "Sockaddr");

    if (O_GET_TYPE(obj) != type) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::ID],
                            "expected a '%s' address, got '%s'",
                            type->name,
                            obj);

        return nullptr;
    }

    return (Sockaddr *) obj;
}

/// Read an optional timeout argument, in milliseconds.
static bool NetCheckTimeout(orbiter::Isolate *isolate, OObject *obj, IntegerUnderlying *out) {
    *out = 0;

    if (O_IS_SENTINEL(obj))
        return true;

    if (!NumberExtract(obj, *out))
        return false;

    if (*out < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "timeout must be non-negative, got %lld",
                 (long long) *out);

        return false;
    }

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

// *** SOCKADDR ***

static OObject *SockaddrToString(orbiter::Isolate *isolate, const OObject *self) {
    char ip_string[NI_MAXHOST];

    auto *s = (Sockaddr *) self;

#ifndef _ORBIT_PLATFORM_WINDOWS
    if (s->addr.ss_family == AF_UNIX)
        return SockaddrUnixToString(isolate, s);
#endif

    int port;
    if (s->addr.ss_family == AF_INET)
        port = ((sockaddr_in *) &s->addr)->sin_port;
    else
        port = ((sockaddr_in6 *) &s->addr)->sin6_port;

    const int err = getnameinfo((sockaddr *) &s->addr,
                                s->length,
                                ip_string,
                                sizeof(ip_string),
                                nullptr,
                                0,
                                NI_NUMERICHOST);
    if (err != 0) {
        SocketSetGaiError(isolate, err, "getnameinfo");

        return nullptr;
    }

    const auto family = SocketInetFamilyToAtom(isolate, s->addr.ss_family);
    if (!family)
        return nullptr;

    return (OObject *) ORStringFormat(isolate, "%s(%s, %d, @%s)",
                                      O_GET_TYPE(self)->name,
                                      ip_string,
                                      ntohs(port),
                                      ORSTRING_TO_CSTR(family->id)).get();
}

// *** TCP HANDLE ***

/// A handle reclaimed with its socket still open hands it back to the loop,
/// which cancels whatever was pending on it and closes it. Closing is
/// thread-safe in gyro, so it is safe from wherever the collector runs.
static bool TCPHandleDtor(TCPHandle *self) {
    if (self->handle != nullptr) {
        gyro_handle_close(GYRO_HANDLE(self->handle), nullptr);

        self->handle = nullptr;
    }

    return true;
}

/// Shared tail of the operations that hand back the handle they were given:
/// closes it when the operation failed, since a half-open socket is of no use
/// to anyone, and publishes it otherwise.
static bool TCPHandleResume(orbiter::Fiber *fiber) {
    const auto handle = std::move(fiber->io.object);

    auto *self = (TCPHandle *) handle.get();

    if (fiber->io.status != GYRO_COMPLETED) {
        TCPHandleDtor(self);

        orbiter::EVLRaiseError(fiber, fiber->io.status);

        return false;
    }

    fiber->SetRRValue(handle.get());
    fiber->AddIP();

    return true;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

// *** SOCKADDR ***

RUNTIME_METHOD(sockaddr_unpack, unpack,
               R"DOC(
@brief Return the address as a (host, port, family) tuple.

The host is the address in its numeric textual form, never a hostname: no
reverse lookup is performed, so the call does not touch the network and never
blocks. An IPv6 address bound to a particular interface keeps its zone suffix,
as in `fe80::1%en0`.

A @UNIX address has a path where the others have a host, and no port at all,
so its port comes back as nil; an unnamed socket has an empty path.

@return A tuple of the host string, the port number and the family atom.

@panic OOMError   When memory allocation fails.
@panic ValueError When the address belongs to a family that has no host and
                  port form.

@example
    parse("127.0.0.1", 8080).unpack()           // ("127.0.0.1", 8080, @INET)
    parse("::1", 80, family=@INET6).unpack()     // ("::1", 80, @INET6)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params);
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    const auto *self = (const Sockaddr *) argv[0];

    const auto family = SocketInetFamilyToAtom(isolate, self->addr.ss_family);
    if (!family)
        return {};

    HOObject o_host;
    HOObject o_port;

    switch (self->addr.ss_family) {
        case AF_INET:
        case AF_INET6: {
            const auto port = self->addr.ss_family == AF_INET
                                  ? ntohs(((const sockaddr_in *) &self->addr)->sin_port)
                                  : ntohs(((const sockaddr_in6 *) &self->addr)->sin6_port);

            char ip_string[NI_MAXHOST];

            const int err = getnameinfo((const sockaddr *) &self->addr,
                                        self->length,
                                        ip_string,
                                        sizeof(ip_string),
                                        nullptr,
                                        0,
                                        NI_NUMERICHOST);
            if (err != 0) {
                SocketSetGaiError(isolate, err, "getnameinfo");

                return {};
            }

            auto host = ORStringNew(isolate, ip_string);
            if (!host)
                return {};

            auto number = IntNew(isolate, port);
            if (!number)
                return {};

            o_host = HOObject(std::move(host));
            o_port = HOObject(std::move(number));

            break;
        }
#ifndef _ORBIT_PLATFORM_WINDOWS
        case AF_UNIX: {
            auto path = SockaddrUnixPath(isolate, self);
            if (!path)
                return {};

            o_host = HOObject(std::move(path));
            o_port = HOObject(kOddBallNIL);

            break;
        }
#endif
        default:
            // Unreachable: every family the atom mapping knows is handled above.
            ErrorSet(isolate,
                     ValueError::Details[ValueError::ID],
                     nullptr,
                     "a @%s address has no host and port",
                     ORSTRING_TO_CSTR(family->id));

            return {};
    }

    auto tuple = TupleNew(isolate, 3);
    if (!tuple)
        return {};

    TupleAppend(tuple.get(), o_host.get());
    TupleAppend(tuple.get(), o_port.get());
    TupleAppend(tuple.get(), (OObject *) family.get());

    return HOObject(std::move(tuple));
}

// *** TCP HANDLE ***

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

RUNTIME_FUNCTION(tcp_connect, connect,
                 R"DOC(
@brief Open a TCP connection to `addr`.

The fiber is parked until the connection is established, so the scheduler
thread stays free for other fibers; only this fiber waits. A connection over
the loopback interface is often established at once, and then nothing is
parked at all.

A handle returned by this call is connected and ready to read and write. It
owns its socket: close it when you are done with it, or let it be collected,
which closes it for you.

@param addr       The peer to reach, as a Sockaddr.
@param timeout=0  Milliseconds to wait, 0 to wait as long as the system does.
                  A connection is not established any sooner by giving up on
                  it earlier, so this says how long the caller is prepared to
                  wait, not what the network will do.

@return A connected TCPHandle.

@panic OOMError   When memory allocation fails.
@panic TypeError  When a parameter has an invalid type.
@panic ValueError When `timeout` is negative.
@panic OSError    When the connection fails, with the reason the system gave:
                  refused, unreachable, timed out.

@example
    h := TCPHandle.connect(parse("127.0.0.1", 8080))
    h := TCPHandle.connect(addr, timeout=5000)
)DOC", 1, "timeout", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("addr", false, InstanceType::OBJECT),
                   PCHECK_DEF("timeout", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    const auto *module = _func->shared->module;

    const auto *addr = NetCheckSockaddr(isolate, module, argv[0]);
    if (addr == nullptr)
        return {};

    IntegerUnderlying timeout;
    if (!NetCheckTimeout(isolate, argv[1], &timeout))
        return {};

    auto *fiber = orbiter::Fiber::Current();
    auto *orbiter = orbiter::Orbiter::GetInstance();

    auto *handle = MakeObject<TCPHandle>(NetExportedType(module, "TCPHandle"), 0);
    if (handle == nullptr)
        return {};

    handle->handle = nullptr;

    const auto result = HOObject((OObject *) handle);

    isolate->gc->Track((OObject *) handle, false);

    handle->handle = gyro_tcp_new(orbiter->GetEventLoop());
    if (handle->handle == nullptr) {
        ErrorSet(isolate,
                 OSError::Details[OSError::ID],
                 nullptr,
                 OSError::Details[OSError::NO_MEMORY],
                 "connect");

        return {};
    }

    fiber->PrepareForEventLoop(TCPHandleResume);
    fiber->io.object = result;

    const auto rc = gyro_tcp_connect(handle->handle,
                                     (const sockaddr *) &addr->addr,
                                     addr->length,
                                     timeout,
                                     orbiter::ResumeFromEventLoop,
                                     fiber,
                                     nullptr);
    if (rc == GYRO_COMPLETED) {
        fiber->AbortEventLoop();

        return result;
    }

    if (rc < 0) {
        fiber->AbortEventLoop();

        orbiter::EVLRaiseError(fiber, rc);

        return {};
    }

    return HOObject(kOddBallNIL);
}

constexpr FunctionDef tcphandle_methods[] = {
    tcp_connect,

    FUNCTIONDEF_SENTINEL
};

constexpr FunctionDef sockaddr_methods[] = {
    sockaddr_unpack,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

constexpr ModuleEntry net_entries[] = {
    ORBIT_MODULE_EXPORT_ALIAS("Sockaddr", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("TCPHandle", nullptr),
    ORBIT_MODULE_EXPORT_FUNCTION(socket_parse),

    ORBIT_MODULE_SENTINEL
};

static bool ModuleNetInit(Module *self) {
    auto *isolate = O_GET_ISOLATE(self);
    const auto *type = O_GET_TYPE(self);

    // Sockaddr

    const auto tp_handle = MakeType(isolate, "Sockaddr", InstanceType::OBJECT,
                                    sizeof(Sockaddr) - sizeof(OObject), 1,
                                    0);
    if (!tp_handle)
        return false;

    ((TypeInfoOps *) tp_handle.get())->ops.to_string = SockaddrToString;

    if (!TIPropertyAdd(tp_handle.get(), sockaddr_methods, self, PropertyFlag::IS_PUBLIC))
        return false;

    if (!ModuleSetLocalProperty(type, nullptr, (OObject *) tp_handle.get()))
        return false;

    // TCPHandle

    const auto tp_tcp = MakeType(isolate, "TCPHandle", InstanceType::OBJECT,
                                 sizeof(TCPHandle) - sizeof(OObject), 1,
                                 0);
    if (!tp_tcp)
        return false;

    tp_tcp->dtor = (DtorFn) TCPHandleDtor;

    if (!TIPropertyAdd(tp_tcp.get(), tcphandle_methods, self, PropertyFlag::IS_PUBLIC))
        return false;

    if (!ModuleSetLocalProperty(type, nullptr, (OObject *) tp_tcp.get()))
        return false;

    return true;
}

ModuleInit ModuleNet = {
    "::orbit::net",
    "@brief Sockets and socket addresses."
    "\n\n"
    "Builds the addresses the socket modules take and return. An address is an "
    "endpoint, the host together with its port, kept in the form the operating "
    "system expects so that nothing is lost in translation.",
    "1.0.0",
    net_entries,
    ModuleNetInit,
    nullptr
};

const ModuleInit *orbiter::module::module_net_ = &ModuleNet;
