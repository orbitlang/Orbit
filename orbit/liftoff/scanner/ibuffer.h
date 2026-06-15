// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SCANNER_IBUFFER_H_
#define ORBIT_LIFTOFF_SCANNER_IBUFFER_H_

#include <cstdio>

#include <orbit/orbiter/isolate.h>

namespace liftoff::scanner {
    constexpr int kLastLineSize = 1024;

    class InputBuffer {
        orbiter::memory::IsolateAllocator allocator_;

        unsigned char *buffer_ = nullptr;
        unsigned char *last_line_ = nullptr;

        size_t b_wr_ = 0;
        size_t b_cur_ = 0;
        size_t b_length_ = 0;

        size_t ll_size_ = kLastLineSize;
        size_t ll_cur_ = 0;
        size_t ll_end_ = 0;

        bool file_ = false;
        bool release_ = true;
        bool fd_error_ = false;

    public:
        InputBuffer(orbiter::Isolate *isolate, const unsigned char *buffer, unsigned long length) : allocator_(isolate),
            buffer_((unsigned char *) buffer),
            b_length_(length),
            release_(false) {
        }

        InputBuffer(orbiter::Isolate *isolate, size_t buf_size, size_t last_line) : allocator_(isolate),
            b_length_(buf_size),
            ll_size_(last_line),
            file_(true) {
        }

        explicit InputBuffer(orbiter::Isolate *isolate, size_t buf_size) : allocator_(isolate),
                                                                           b_length_(buf_size),
                                                                           file_(true) {
        }

        ~InputBuffer();

        bool AppendInput(const unsigned char *buffer, int length);

        [[nodiscard]] char *GetCurrentLine(int *out_len);

        int Peek(bool advance);

        int ReadFile(FILE *fd);
    };
}

#endif // !ORBIT_LIFTOFF_SCANNER_IBUFFER_H_
