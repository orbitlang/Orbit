// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cerrno>
#include <cstdio>

#include <orbit/util/macros.h>

#ifdef _ORBIT_PLATFORM_WINDOWS
#include <io.h>
#include <fcntl.h>
#else
#include <sys/fcntl.h>
#include <unistd.h>
#endif

#include <orbit/orbiter/datatype/bytes.h>
#include <orbit/orbiter/datatype/byteview.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/module/modules.h>

using namespace orbiter::datatype;
using namespace orbiter::module;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

/// Convenience wrapper around `ErrorSetFromErrno` for the common fd-based
/// case: formats `"fd N"` as the context, sets the panic and returns an empty
/// HOObject so the call site stays a one-liner: `return ErrorFromFd(...)`.
static HOObject ErrorFromFd(orbiter::Isolate *isolate, const IntegerUnderlying fd) {
    char ctx[32];

    std::snprintf(ctx, sizeof(ctx), "fd %d", (int) fd);

    ErrorSetFromErrno(isolate, ctx);

    return {};
}

// *********************************************************************************************************************
// PRIMITIVES
// *********************************************************************************************************************

RUNTIME_FUNCTION(io_close, close,
                 R"DOC(
@brief Close an open file descriptor.

After this call fd must not be used again. Closing an already-closed or
never-opened fd raises `OSError`.

@param fd  The file descriptor to close.

@panic OSError  When the underlying close(2) fails (typically `BAD_FD`).
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto rc = ::_close((int) fd);
#else
    const auto rc = ::close((int) fd);
#endif

    if (rc < 0)
        return ErrorFromFd(isolate, fd);

    return HOObject(kOddBallNIL);
}

RUNTIME_FUNCTION(io_dup, dup,
                 R"DOC(
@brief Duplicate fd, returning a new descriptor that refers to the same
open file description.

Both descriptors share the underlying file offset and the open-mode
flags — a read or seek on one is visible from the other. They differ
only in the per-descriptor close-on-exec setting.

By default the new descriptor is created with `FD_CLOEXEC` set, so it
will be closed automatically when the process `exec()`s another
program — the safer default for most code. Pass `cloexec=false` only
when the descriptor must be deliberately inherited across `exec`,
typically when setting up stdin / stdout / stderr for a child process
in a fork+exec pair.

On POSIX this is implemented via `fcntl(F_DUPFD_CLOEXEC, 0)` when
`cloexec=true`, or plain `dup(2)` otherwise. Windows has no direct
equivalent at the C-runtime level, so the `cloexec` argument is
currently ignored there and the descriptor follows the platform
default.

@param fd            The file descriptor to duplicate. Must refer to
                     an open descriptor.
@param cloexec=true  Whether the new descriptor carries the close-on-
                     exec flag. Ignored on Windows.

@return The newly-allocated file descriptor.

@panic OSError  When the underlying syscall fails.

@example
    let copy = dup(fd)                    // safer: closed on exec
    let child_in = dup(fd, cloexec=false) // inherited across exec()
)DOC", 1, "cloexec", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER),
                   PCHECK_DEF("cloexec", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

    const bool cloexec = O_IS_SENTINEL(argv[1]) ? true : OBOOL_TO_BOOL(argv[1]);

#ifdef _ORBIT_PLATFORM_WINDOWS
    // CLOEXEC has no direct equivalent on the Windows CRT.
    const auto new_fd = ::_dup((int) fd);
#else
    const auto new_fd = cloexec
                            ? ::fcntl((int) fd, F_DUPFD_CLOEXEC, 0)
                            : ::dup((int) fd);
#endif

    if (new_fd < 0)
        return ErrorFromFd(O_GET_ISOLATE(_func), fd);

    return HOObject((OObject *) O_TO_SMI((MSSize) new_fd));
}

RUNTIME_FUNCTION(io_flush, flush,
                 R"DOC(
@brief Flush pending writes for fd to the OS / disk.

Calls `fsync(2)` on POSIX, `_commit` on Windows. For file-descriptor IO
there is no user-space buffer to flush, so this is effectively the same
synchronous "force to disk" call as `fsync` — exposed under both names
for API symmetry.

@param fd  The file descriptor to flush.

@panic OSError  When the underlying syscall fails.

@see fsync
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto rc = ::_commit((int) fd);
#else
    const auto rc = ::fsync((int) fd);
#endif

    if (rc < 0)
        return ErrorFromFd(isolate, fd);

    return HOObject(kOddBallNIL);
}

RUNTIME_FUNCTION(io_fsync, fsync,
                 R"DOC(
@brief Force pending writes for fd all the way to physical storage.

Same call as `flush` on POSIX systems; provided as a distinct name so
intent is explicit at call sites (durability requirement vs. plain
"finish writing").

@param fd  The file descriptor to sync.

@panic OSError  When the underlying syscall fails.

@see flush
)DOC", 1, nullptr, false, false) {
    return io_flush_fn(_func, argv, rest, kwargs, argc);
}

RUNTIME_FUNCTION(io_isatty, isatty,
                 R"DOC(
@brief Return true if fd refers to an interactive terminal.

Useful for deciding whether to emit colour escape codes, prompts, etc.

@param fd  The file descriptor to probe.

@return true if fd is a TTY, false otherwise. Does not raise on a
        non-TTY (or even on an invalid fd) — returns false.
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto tty = ::_isatty((int) fd);
#else
    const auto tty = ::isatty((int) fd);
#endif

    return HOObject((OObject *) BOOL_TO_OBOOL(tty != 0));
}

RUNTIME_FUNCTION(io_open, open,
                 R"DOC(
@brief Open the file at path and return a file descriptor.

@param flags  Bitmask of `O_READ`, `O_WRITE`, `O_RW`, `O_CREAT`,
              `O_TRUNC`, `O_APPEND`, `O_EXCL`. Defaults to `O_READ`.
@param mode   Unix permission bits for the file when `O_CREAT` creates
              it. Ignored on Windows. Defaults to 0o644.
@param path   The filesystem path.

@return The new file descriptor (a non-negative integer).

@panic OSError  When the underlying open(2) fails — typically
                `NOT_FOUND`, `PERMISSION_DENIED`, `ALREADY_EXISTS`.

@example
    let fd = open("hello.txt")                                  // read-only
    let fd = open("out.txt", flags=O_WRITE | O_CREAT | O_TRUNC) // truncate-or-create
    let fd = open("log.txt", flags=O_WRITE | O_CREAT | O_APPEND, mode=0o600)
)DOC", 1, "flags, mode", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("path", false, InstanceType::STRING),
                   PCHECK_DEF("flags", true, InstanceType::NUMBER),
                   PCHECK_DEF("mode", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    const auto *path = ORSTRING_TO_CSTR((const ORString *) argv[0]);

    IntegerUnderlying flags = O_RDONLY;
    if (!O_IS_SENTINEL(argv[1]) && !NumberExtract(argv[1], flags))
        return {};

    IntegerUnderlying mode = 0644;
    if (!O_IS_SENTINEL(argv[2]) && !NumberExtract(argv[2], mode))
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto fd = ::_open(path, (int) flags, (int) mode);
#else
    const auto fd = ::open(path, (int) flags, (mode_t) mode);
#endif

    if (fd < 0) {
        ErrorSetFromErrno(isolate, path);

        return {};
    }

    return HOObject((OObject *) O_TO_SMI((MSSize) fd));
}

RUNTIME_FUNCTION(io_read, read,
                 R"DOC(
@brief Read up to n bytes from fd.

May return fewer than n bytes when the underlying device returns a
short read (slow pipes, end of file, …). An empty Bytes signals EOF.

@param fd  The file descriptor to read from.
@param n   Maximum number of bytes to read. Must be non-negative.

@return A mutable Bytes containing the bytes actually read.

@panic ValueError  When n is negative.
@panic OSError     When the underlying read(2) fails.
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER),
                   PCHECK_DEF("n", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

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

    // Allocate the destination Bytes with capacity `n`; we'll read directly
    // into its backing buffer and set `length` to what the syscall returned.
    const auto out = BytesNew(isolate, n, false);
    if (!out)
        return {};

    auto *buf = out->shared->buffer + out->start;

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto got = ::_read((int) fd, buf, (unsigned int) n);
#else
    const auto got = ::read((int) fd, buf, (size_t) n);
#endif

    if (got < 0) {
        return ErrorFromFd(isolate, fd);
    }

    out->length = (MSize) got;

    return HOObject((OObject *) out.get());
}

RUNTIME_FUNCTION(io_readinto, readinto,
                 R"DOC(
@brief Read up to length bytes from fd into buf at the given offset.

The destination buf must already be at least `offset + length` bytes long
— `readinto` does NOT grow it. May return fewer than length bytes on a
short read; bytes beyond what was actually read are left untouched. A
return of 0 means EOF.

@param fd      The file descriptor to read from.
@param buf     The mutable Bytes to write into.
@param offset  Position in buf where the first byte lands (>= 0).
@param length  Maximum number of bytes to read (>= 0).

@return The number of bytes actually read; 0 on EOF.

@panic ValueError  When buf is frozen, when offset or length is negative,
                   or when offset + length exceeds the current size of buf.
@panic OSError     When the underlying read(2) fails.

@see read

@example
    let buf = Bytes(len=1024)
    let n   = readinto(fd, buf, 0, 1024) // 0..n holds the bytes that were just read
)DOC", 4, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER),
                   PCHECK_DEF("buf", false, InstanceType::BYTES),
                   PCHECK_DEF("offset", false, InstanceType::NUMBER),
                   PCHECK_DEF("length", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

    auto *buf = (Bytes *) argv[1];

    IntegerUnderlying offset;
    if (!NumberExtract(argv[2], offset))
        return {};

    IntegerUnderlying length;
    if (!NumberExtract(argv[3], length))
        return {};

    if (offset < 0 || length < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "readinto offset and length must be non-negative");

        return {};
    }

    BytesWriteGuard guard(buf, offset, length);
    if (!guard)
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto got = ::_read((int) fd, guard.Data(), (unsigned int) length);
#else
    const auto got = ::read((int) fd, guard.Data(), length);
#endif

    guard.Release();

    if (got < 0)
        return ErrorFromFd(isolate, fd);

    return HOObject((OObject *) O_TO_SMI((MSSize) got));
}

RUNTIME_FUNCTION(io_seek, seek,
                 R"DOC(
@brief Reposition the read/write offset of fd.

@param fd      The file descriptor.
@param offset  Byte offset relative to whence.
@param whence  One of `SEEK_SET` (absolute), `SEEK_CUR` (relative to
               current position), `SEEK_END` (relative to end of file).

@return The new absolute offset from the start of the file.

@panic OSError  When the underlying lseek(2) fails.
)DOC", 3, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER),
                   PCHECK_DEF("offset", false, InstanceType::NUMBER),
                   PCHECK_DEF("whence", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

    IntegerUnderlying offset;
    if (!NumberExtract(argv[1], offset))
        return {};

    IntegerUnderlying whence;
    if (!NumberExtract(argv[2], whence))
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto pos = ::_lseeki64((int) fd, offset, (int) whence);
#else
    const auto pos = ::lseek((int) fd, offset, (int) whence);
#endif

    if (pos < 0)
        return ErrorFromFd(isolate, fd);

    return HOObject((OObject *) O_TO_SMI((MSSize) pos));
}

RUNTIME_FUNCTION(io_write, write,
                 R"DOC(
@brief Write bytes to a file descriptor.

May write fewer bytes than `data.length` — slow pipes, signals, or kernel
buffer pressure can cause a short write. The caller is responsible for
retrying the remainder.

@param fd    The file descriptor to write to.
@param data  The bytes to write.

@return The number of bytes actually written.

@panic OSError  When the underlying write(2) fails.

@example
    write(STDOUT, b"hello\n")  // → 6
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("fd", false, InstanceType::NUMBER),
                   PCHECK_DEF("data", false, InstanceType::BYTES, InstanceType::STRING));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying fd;
    if (!NumberExtract(argv[0], fd))
        return {};

    // Holds the Bytes read lock for the duration of the syscall, so a
    // concurrent enlarge cannot reallocate the buffer mid-write.
    const ByteView data(isolate, argv[1]);
    if (!data)
        return {};

#ifdef _ORBIT_PLATFORM_WINDOWS
    const auto written = ::_write((int) fd, data.Data(), (unsigned int) data.Size());
#else
    const auto written = ::write((int) fd, data.Data(), data.Size());
#endif

    if (written < 0)
        return ErrorFromFd(isolate, fd);

    return HOObject((OObject *) O_TO_SMI((MSSize) written));
}

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

const ModuleEntry io_entries[] = {
    ORBIT_MODULE_EXPORT_FUNCTION(io_close),
    ORBIT_MODULE_EXPORT_FUNCTION(io_dup),
    ORBIT_MODULE_EXPORT_FUNCTION(io_flush),
    ORBIT_MODULE_EXPORT_FUNCTION(io_fsync),
    ORBIT_MODULE_EXPORT_FUNCTION(io_isatty),
    ORBIT_MODULE_EXPORT_FUNCTION(io_open),
    ORBIT_MODULE_EXPORT_FUNCTION(io_read),
    ORBIT_MODULE_EXPORT_FUNCTION(io_readinto),
    ORBIT_MODULE_EXPORT_FUNCTION(io_seek),
    ORBIT_MODULE_EXPORT_FUNCTION(io_write),

    // Open() flags.
    ORBIT_MODULE_EXPORT_ALIAS("O_READ", O_TO_SMI(O_RDONLY)),
    ORBIT_MODULE_EXPORT_ALIAS("O_WRITE", O_TO_SMI(O_WRONLY)),
    ORBIT_MODULE_EXPORT_ALIAS("O_RW", O_TO_SMI(O_RDWR)),

    ORBIT_MODULE_EXPORT_ALIAS("O_APPEND", O_TO_SMI(O_APPEND)),
    ORBIT_MODULE_EXPORT_ALIAS("O_CREAT", O_TO_SMI(O_CREAT)),
    ORBIT_MODULE_EXPORT_ALIAS("O_EXCL", O_TO_SMI(O_EXCL)),
    ORBIT_MODULE_EXPORT_ALIAS("O_TRUNC", O_TO_SMI(O_TRUNC)),

    // seek() whence.
    ORBIT_MODULE_EXPORT_ALIAS("SEEK_CUR", O_TO_SMI(SEEK_CUR)),
    ORBIT_MODULE_EXPORT_ALIAS("SEEK_END", O_TO_SMI(SEEK_END)),
    ORBIT_MODULE_EXPORT_ALIAS("SEEK_SET", O_TO_SMI(SEEK_SET)),

    // Standard streams.
#ifdef _ORBIT_PLATFORM_WINDOWS
    ORBIT_MODULE_EXPORT_ALIAS("STDIN", O_TO_SMI(_fileno(stdin))),
    ORBIT_MODULE_EXPORT_ALIAS("STDOUT", O_TO_SMI(_fileno(stdout))),
    ORBIT_MODULE_EXPORT_ALIAS("STDERR", O_TO_SMI(_fileno(stderr))),
#else
    ORBIT_MODULE_EXPORT_ALIAS("STDIN", O_TO_SMI(STDIN_FILENO)),
    ORBIT_MODULE_EXPORT_ALIAS("STDOUT", O_TO_SMI(STDOUT_FILENO)),
    ORBIT_MODULE_EXPORT_ALIAS("STDERR", O_TO_SMI(STDERR_FILENO)),
#endif

    ORBIT_MODULE_SENTINEL
};

ModuleInit ModuleIO = {
    "::orbit::io",
    "@brief Basic I/O primitives."
    "\n\n"
    "Provides file-descriptor based I/O operations (open, read, write, close), "
    "standard streams (stdin/stdout/stderr), seek positioning, and TTY detection. "
    "Follows POSIX-style conventions with cross-platform compatibility.",
    "1.0.0",
    io_entries,
    nullptr,
    nullptr
};

const ModuleInit *orbiter::module::module_io_ = &ModuleIO;
