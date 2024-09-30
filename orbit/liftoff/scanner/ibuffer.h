// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SCANNER_IBUFFER_H_
#define ORBIT_LIFTOFF_SCANNER_IBUFFER_H_

#include <cstdio>

namespace liftoff::scanner {
    constexpr const int kLastLineSize = 1024;

    class InputBuffer {
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

    public:
        InputBuffer(const unsigned char *buffer, unsigned long length) : buffer_((unsigned char *) buffer),
                                                                         b_length_(length),
                                                                         release_(false) {}

        InputBuffer(size_t buf_size, size_t last_line) : b_length_(buf_size), ll_size_(last_line), file_(true) {}

        explicit InputBuffer(size_t buf_size) : b_length_(buf_size), file_(true) {}

        InputBuffer() = default;

        ~InputBuffer();

        bool AppendInput(const unsigned char *buffer, int length);

        [[nodiscard]] char *GetCurrentLine(int *out_len) const;

        int Peek(bool advance);

        int ReadFile(FILE *fd);
    };
}

#endif // !ORBIT_LIFTOFF_SCANNER_IBUFFER_H_
