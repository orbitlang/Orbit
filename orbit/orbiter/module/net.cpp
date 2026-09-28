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
#include <climits>
#include <cstddef>
#include <cstring>

#include <orbit/orbiter/evloop.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/bytes.h>
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

/// Connections the kernel may hold for a listening socket before it starts refusing them.
constexpr auto kTCPDefaultBacklog = 128;

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

static bool TCPHandleDtor(TCPHandle *self) {
    if (self->handle != nullptr) {
        gyro_handle_close(GYRO_HANDLE(self->handle), nullptr);

        self->handle = nullptr;
    }

    return true;
}

/// Look up one of the module's exported types by name. The module exports them
/// itself, so a missing entry is a bug in ModuleNetInit rather than a runtime
/// condition.
static TypeInfo *NetExportedType(const Module *module, const char *name) {
    const auto *prop = TIFindLocalProperty(O_GET_TYPE(module), name);

    assert(prop != nullptr);

    return (TypeInfo *) prop->value;
}

/// Wrap a raw address in one of this module's Sockaddr objects.
static HOObject NetSockaddrNew(const orbiter::Isolate *isolate, const Module *module, const sockaddr_storage *addr,
                               const socklen_t length) {
    auto *saddr = MakeObject<Sockaddr>(NetExportedType(module, "Sockaddr"), 0);
    if (saddr == nullptr)
        return {};

    saddr->addr = *addr;
    saddr->length = length;

    O_GC_TRACK_RETURN(isolate, (OObject*)saddr, false);
}

static gyro_socket_t NetHandleSocket(orbiter::Isolate *isolate, const TCPHandle *self, const char *context) {
    if (self->handle != nullptr) {
        const auto socket = gyro_tcp_fileno(self->handle);

        if (socket != GYRO_INVALID_SOCKET)
            return socket;
    }

    ErrorSet(isolate,
             OSError::Details[OSError::ID],
             nullptr,
             OSError::Details[OSError::BAD_FD],
             context);

    return GYRO_INVALID_SOCKET;
}

/// The gyro handle behind @p self, or nullptr with an OSError set when it has
/// already been closed.
static gyro_tcp_t *NetHandleRequire(orbiter::Isolate *isolate, const TCPHandle *self, const char *context) {
    if (self->handle != nullptr)
        return self->handle;

    ErrorSet(isolate,
             OSError::Details[OSError::ID],
             nullptr,
             OSError::Details[OSError::BAD_FD],
             context);

    return nullptr;
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

constexpr FunctionDef sockaddr_methods[] = {
    sockaddr_unpack,

    FUNCTIONDEF_SENTINEL
};

// *** TCP HANDLE ***

RUNTIME_METHOD(tcp_bind, bind,
               R"DOC(
@brief Bind the socket to a local address.

The system answers at once, so nothing is parked. Bind to port 0 to let it pick
a free port, and read back which one with `local_addr()`; bind to the wildcard
address of the family to take every interface.

@param addr     The local address, as a Sockaddr. Its family must be the one
                the handle was opened for.
@param flags=0  Zero, or TCP_REUSEADDR to allow binding an address still held
                by the connections of a previous run.

@return Self, so that `listen()` or `connect()` can be chained.

@panic OSError When the handle is closed, or the address cannot be bound: it is
               in use, or not one of this host's, or the port is privileged.

@example
    h := TCPHandle.open(@INET)
    h.bind(parse("127.0.0.1", 8080), flags=TCP_REUSEADDR)
)DOC", 2, "flags", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("addr", false, InstanceType::OBJECT),
                   PCHECK_DEF("flags", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    const auto *module = _func->shared->module;

    const auto *self = (const TCPHandle *) argv[0];

    const auto *addr = NetCheckSockaddr(isolate, module, argv[1]);
    if (addr == nullptr)
        return {};

    IntegerUnderlying flags = 0;
    if (!O_IS_SENTINEL(argv[2]) && !NumberExtract(argv[2], flags))
        return {};

    auto *handle = NetHandleRequire(isolate, self, "bind");
    if (handle == nullptr)
        return {};

    const auto rc = gyro_tcp_bind(handle,
                                  (const sockaddr *) &addr->addr,
                                  addr->length,
                                  (unsigned int) flags);
    if (rc < 0) {
        orbiter::EVLRaiseError(orbiter::Fiber::Current(), rc);

        return {};
    }

    return HOObject((OObject *) self);
}

RUNTIME_METHOD(tcp_connect, connect,
               R"DOC(
@brief Connect the socket to a peer.

The fiber is parked until the connection is established, so the scheduler
thread stays free for other fibers; only this fiber waits. A connection over
the loopback interface is often established at once, and then nothing is parked
at all.

@param addr       The peer to reach, as a Sockaddr.
@param timeout=0  Milliseconds to wait, 0 to wait as long as the system does.

@return Self, once the connection is established.

@panic OSError When the handle is closed, or the connection fails with the
               reason the system gave: refused, unreachable, timed out. The
               socket is left open, so it can be closed or tried again.

@example
    h := TCPHandle.open(@INET)
    h.connect(addr, timeout=5000)
)DOC", 2, "timeout", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("addr", false, InstanceType::OBJECT),
                   PCHECK_DEF("timeout", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    const auto *module = _func->shared->module;

    auto *self = (TCPHandle *) argv[0];

    const auto *addr = NetCheckSockaddr(isolate, module, argv[1]);
    if (addr == nullptr)
        return {};

    IntegerUnderlying timeout;
    if (!NetCheckTimeout(isolate, argv[2], &timeout))
        return {};

    auto *handle = NetHandleRequire(isolate, self, "connect");
    if (handle == nullptr)
        return {};

    auto *fiber = orbiter::Fiber::Current();

    fiber->PrepareForEventLoop(orbiter::EVLDoNothingAddIP, (OObject *) self);

    const auto rc = gyro_tcp_connect(handle,
                                     (const sockaddr *) &addr->addr,
                                     addr->length,
                                     timeout,
                                     orbiter::ResumeFromEventLoop,
                                     fiber,
                                     nullptr);

    // Established inside the call, which is the usual outcome over loopback: no
    // callback will follow, so the fiber carries on.
    if (rc == GYRO_COMPLETED) {
        fiber->AbortEventLoop();

        return HOObject((OObject *) self);
    }

    if (rc < 0) {
        fiber->AbortEventLoop();

        orbiter::EVLRaiseError(fiber, rc);

        return {};
    }

    return HOObject((OObject *) self);
}

RUNTIME_METHOD(tcp_fileno, fileno,
               R"DOC(
@brief Return the socket the handle is built on.

For what this module does not wrap: a socket option it has no call for, or a
question only the system can answer about the socket.

DO NOT read from it, write to it, or close it. The event loop owns the
readiness of that socket and the operations queued against it: reading behind
its back takes bytes belonging to a queued read, and closing it hands the
number back to the system while the loop is still watching it, which is how an
unrelated file ends up being watched in its place. Use `close()` instead.

@return The socket, as the number the system knows it by.

@panic OSError When the handle is closed, or its socket was never opened.

@example
    h := TCPHandle.open(@INET).bind(parse("127.0.0.1", 0)).listen()
    h.fileno()        // 7
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params);
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    const auto *self = (const TCPHandle *) argv[0];

    const auto socket = NetHandleSocket(isolate, self, "fileno");
    if (socket == GYRO_INVALID_SOCKET)
        return {};

    return HOObject(IntNew(isolate, socket));
}

RUNTIME_METHOD(tcp_listen, listen,
               R"DOC(
@brief Start accepting connections on the bound socket.

Only marks the socket as listening, which the system answers at once;
connections are taken one at a time with `accept()`.

Binding first is what chooses the address to listen on. Listening on a socket
that was never bound is allowed, and the system binds it for you to an arbitrary
port on every interface, which `local_addr()` then reports.

@param backlog=128  Connections the kernel may hold before refusing more.

@return Self.

@panic ValueError When `backlog` is negative or larger than the system allows.
@panic OSError    When the handle is closed, or the socket cannot listen.

@example
    srv := TCPHandle.open(@INET).bind(parse("127.0.0.1", 8080)).listen()
    srv.local_addr().unpack()        // ("127.0.0.1", 8080, @INET)
)DOC", 1, "backlog", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("backlog", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    const auto *self = (const TCPHandle *) argv[0];

    IntegerUnderlying backlog = kTCPDefaultBacklog;
    if (!O_IS_SENTINEL(argv[1])) {
        if (!NumberExtract(argv[1], backlog))
            return {};

        if (backlog < 0 || backlog > INT_MAX) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::ID],
                     nullptr,
                     "backlog must be in 0..%d, got %lld",
                     INT_MAX,
                     (long long) backlog);

            return {};
        }
    }

    const auto *handle = NetHandleRequire(isolate, self, "listen");
    if (handle == nullptr)
        return {};

    const auto rc = gyro_tcp_listen(handle, (int) backlog);
    if (rc < 0) {
        orbiter::EVLRaiseError(orbiter::Fiber::Current(), rc);

        return {};
    }

    return HOObject((OObject *) self);
}

RUNTIME_METHOD(tcp_local_addr, local_addr,
               R"DOC(
@brief Return the local address the socket is bound to.

Answers for a listener as well as for a connection, and is the way to learn
which port the system picked when the socket was bound to port 0. The address
is read from the socket itself, so it reflects what the system actually did
rather than what was asked for.

@return The local Sockaddr.

@panic OSError  When the handle is closed, or the address cannot be read.

@example
    h := TCPHandle.open(@INET).bind(parse("127.0.0.1", 0)).listen()
    h.local_addr().unpack()        // ("127.0.0.1", 54321, @INET)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params);
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    const auto *module = _func->shared->module;

    const auto *self = (const TCPHandle *) argv[0];

    const auto socket = NetHandleSocket(isolate, self, "local_addr");
    if (socket == GYRO_INVALID_SOCKET)
        return {};

    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);

    if (getsockname(socket, (sockaddr *) &storage, &length) != 0) {
        ErrorSetFromErrno(isolate, "getsockname");

        return {};
    }

    return NetSockaddrNew(isolate, module, &storage, length);
}

RUNTIME_FUNCTION(tcp_open, open,
                 R"DOC(
@brief Create a TCP socket of the given family.

The socket is open and configured but does nothing yet: this is the moment to
set the options a system only honours before a socket is bound or connected,
reaching it through `fileno()`. Then bind it, or connect it.

The event loop has already set what it needs on that socket: non-blocking mode,
close-on-exec, and no SIGPIPE where that exists. Non-blocking is not
negotiable; clearing it stalls the loop on the first operation that has to wait.

@param family Address family the socket is opened for: @INET or @INET6. Binding
              or connecting an address of another family afterwards is refused
              by the system.

@return A new TCPHandle.

@panic TypeError  When a parameter has an invalid type.
@panic ValueError When `family` is not an address family TCP speaks.
@panic OSError    When the socket cannot be opened, out of descriptors for
                  instance.

@example
    h := TCPHandle.open(@INET)
    h.bind(parse("127.0.0.1", 0))
    h.listen()
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("family", false, InstanceType::ATOM));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    const auto *module = _func->shared->module;

    int family;
    if (!SocketAtomToInetFamily(isolate, (Atom *) argv[0], &family))
        return {};

    if (family != AF_INET && family != AF_INET6) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "a TCP socket is @INET or @INET6, not @%s",
                 ORSTRING_TO_CSTR(((Atom *) argv[0])->id));

        return {};
    }

    const auto *orbiter = orbiter::Orbiter::GetInstance();

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
                 "open");

        return {};
    }

    const auto rc = gyro_tcp_open(handle->handle, family);
    if (rc < 0) {
        orbiter::EVLRaiseError(orbiter::Fiber::Current(), rc);

        return {};
    }

    return result;
}

RUNTIME_METHOD(tcp_peer_addr, peer_addr,
               R"DOC(
@brief Return the address of the peer at the other end of the connection.

Only a connected handle has a peer: asking a listener, which has none, is an
error rather than an empty answer.

@return The remote Sockaddr.

@panic OSError  When the handle is closed, or is not connected.

@example
    c := TCPHandle.open(@INET).connect(parse("127.0.0.1", 8080))
    c.peer_addr().unpack()        // ("127.0.0.1", 8080, @INET)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params);
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    const auto *module = _func->shared->module;

    const auto *self = (const TCPHandle *) argv[0];

    const auto socket = NetHandleSocket(isolate, self, "peer_addr");
    if (socket == GYRO_INVALID_SOCKET)
        return {};

    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);

    if (getpeername(socket, (sockaddr *) &storage, &length) != 0) {
        ErrorSetFromErrno(isolate, "getpeername");

        return {};
    }

    return NetSockaddrNew(isolate, module, &storage, length);
}

RUNTIME_METHOD(tcp_read, read,
               R"DOC(
@brief Read whatever has arrived on the connection, up to `n` bytes.

The read is short by design: it returns as soon as at least one byte is there,
and never waits for `n` of them. Reading an exact amount means reading again
until it is reached. When nothing has arrived yet, the fiber is parked until
data comes in, so the scheduler thread stays free for other fibers; when data
is already waiting, nothing is parked at all.

An empty Bytes means the peer shut the connection down cleanly: no more bytes
are coming, and it is the only way to learn it. Asking for 0 bytes is the one
exception: it answers with an empty Bytes without touching the connection, so
it says nothing about the peer.

@param n          Maximum number of bytes to read.
@param timeout=0  Milliseconds to wait for data, 0 to wait indefinitely.

@return A new Bytes with the bytes read, between 1 and `n` long, or empty at
        end of stream.

@panic TypeError  When a parameter has an invalid type.
@panic ValueError When `n` or `timeout` is negative.
@panic OSError    When the read fails with the reason the system gave: reset
                  by the peer, or timed out.

@example
    c := TCPHandle.open(@INET)
    c.connect(parse("127.0.0.1", 8080))

    data := c.read(4096)
    chunk := c.read(1024, timeout=5000)
)DOC", 2, "timeout", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("n", false, InstanceType::NUMBER),
                   PCHECK_DEF("timeout", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const TCPHandle *) argv[0];

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying n;
    if (!NumberExtract(argv[1], n))
        return {};

    if (n < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "read length cannot be negative"
        );

        return {};
    }

    IntegerUnderlying timeout;
    if (!NetCheckTimeout(isolate, argv[2], &timeout))
        return {};

    auto *handle = NetHandleRequire(isolate, self, "read");
    if (handle == nullptr)
        return {};

    const auto out = BytesNew(isolate, n, false);
    if (!out)
        return {};

    // Nothing was asked for, so nothing is read.
    if (n == 0)
        return HOObject((OObject *) out.get());

    auto *fiber = orbiter::Fiber::Current();

    // The buffer needs no pin: this Bytes was just created here and no Orbit
    // code can name it yet, so nothing can resize it while the loop writes.
    fiber->PrepareForEventLoop(orbiter::EVLReturnFilledBytes, (OObject *) out.get());

    fiber->io.buf.base = (char *) out->shared->buffer;
    fiber->io.buf.len = n;

    size_t transferred = 0;
    const auto rc = gyro_tcp_read(handle,
                                  &fiber->io.buf,
                                  1,
                                  timeout,
                                  orbiter::ResumeFromEventLoop,
                                  fiber,
                                  nullptr,
                                  &transferred);
    if (rc < 0 && rc != GYRO_EOF) {
        fiber->AbortEventLoop();

        orbiter::EVLRaiseError(fiber, rc);

        return {};
    }

    if (rc == GYRO_COMPLETED || rc == GYRO_EOF) {
        fiber->AbortEventLoop();

        out->length = transferred;

        return HOObject((OObject *) out.get());
    }

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(tcp_readinto, readinto,
R"DOC(
@brief Read up to length bytes from the connection into buffer at the given offset.

The destination buffer must already be at least `offset + length` bytes long,
`readinto` does NOT grow it. The read is short by design: it returns as soon
as at least one byte is there, and never waits for `length` of them; bytes
beyond what was actually read are left untouched. A return of 0 means the peer
shut the connection down cleanly: no more bytes are coming. Asking for 0 bytes
is the one exception: it returns 0 without touching the connection, so it says
nothing about the peer.

When nothing has arrived yet, the fiber is parked until data comes in, so the
scheduler thread stays free for other fibers; when data is already waiting,
nothing is parked at all. While the fiber is parked the buffer is written in
place by the event loop: its contents can still be read or changed, but any
attempt to resize it (e.g. `append`) panics with ValueError until the read
completes.

@param buffer     The mutable Bytes to write into.
@param offset     Position in buffer where the first byte lands (>= 0).
@param length     Maximum number of bytes to read (>= 0).
@param timeout=0  Milliseconds to wait for data, 0 to wait indefinitely.

@return The number of bytes actually read; 0 on end of stream.

@panic TypeError  When a parameter has an invalid type.
@panic ValueError When buffer is frozen, when offset, length or timeout is
                  negative, or when offset + length exceeds the current size
                  of buffer.
@panic OSError    When the read fails with the reason the system gave: reset
                  by the peer, or timed out.

@see read

@example
    c := TCPHandle.open(@INET)
    c.connect(parse("127.0.0.1", 8080))

    buf := Bytes(len=1024)
    n   := c.readinto(buf, 0, 1024)    # 0..n holds the bytes that were just read
    n   = c.readinto(buf, 0, 1024, timeout=5000)
)DOC", 4, "timeout", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("buffer", false, InstanceType::BYTES),
                   PCHECK_DEF("offset", false, InstanceType::NUMBER),
                   PCHECK_DEF("length", false, InstanceType::NUMBER),
                   PCHECK_DEF("timeout", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const TCPHandle *) argv[0];

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying r_offset;
    if (!NumberExtract(argv[2], r_offset))
        return {};

    if (r_offset < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "offset cannot be negative"
        );

        return {};
    }

    IntegerUnderlying r_length;
    if (!NumberExtract(argv[3], r_length))
        return {};

    if (r_length < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "read length cannot be negative"
        );

        return {};
    }

    IntegerUnderlying timeout;
    if (!NetCheckTimeout(isolate, argv[4], &timeout))
        return {};

    auto *handle = NetHandleRequire(isolate, self, "readinto");
    if (handle == nullptr)
        return {};

    const auto buffer = BytesWriteGuard((Bytes *) argv[1], r_offset, r_length);
    if (!buffer)
        return {};

    // Nothing was asked for, so nothing is read.
    if (r_length == 0)
        return HOObject((OObject *) O_TO_SMI(0));

    auto *fiber = orbiter::Fiber::Current();

    fiber->PrepareForEventLoop(orbiter::EVLReturnTransferred, argv[1]);

    fiber->io.buf.base = (char *) buffer.Data();
    fiber->io.buf.len = r_length;

    size_t transferred = 0;
    const auto rc = gyro_tcp_read(handle,
                                  &fiber->io.buf,
                                  1,
                                  timeout,
                                  orbiter::ResumeFromEventLoop,
                                  fiber,
                                  nullptr,
                                  &transferred);
    if (rc < 0 && rc != GYRO_EOF) {
        fiber->AbortEventLoop();

        orbiter::EVLRaiseError(fiber, rc);

        return {};
    }

    if (rc == GYRO_COMPLETED || rc == GYRO_EOF) {
        fiber->AbortEventLoop();

        return HOObject((OObject *) O_TO_SMI(transferred));
    }

    buffer.PinBuffer();

    return HOObject(kOddBallNIL);
}

constexpr FunctionDef tcphandle_methods[] = {
    tcp_bind,
    tcp_connect,
    tcp_fileno,
    tcp_listen,
    tcp_local_addr,
    tcp_open,
    tcp_peer_addr,
    tcp_read,
    tcp_readinto,

    FUNCTIONDEF_SENTINEL
};

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

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

const ModuleEntry net_entries[] = {
    ORBIT_MODULE_EXPORT_FUNCTION(socket_parse),

    ORBIT_MODULE_EXPORT_ALIAS("Sockaddr", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("TCPHandle", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("TCP_REUSEADDR", O_TO_SMI(GYRO_TCP_REUSEADDR)),

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
                                 sizeof(TCPHandle) - sizeof(OObject), 9,
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

static ModuleInit ModuleNet = {
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
