// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/memory/memory.h>

#include <orbit/liftoff/scanner/ibuffer.h>

using namespace liftoff::scanner;

InputBuffer::~InputBuffer() {
    if (this->release_)
        orbiter::memory::Free(this->buffer_);

    if (this->file_)
        orbiter::memory::Free(this->last_line_);
}

bool InputBuffer::AppendInput(const unsigned char *buffer, int length) {
    assert(this->release_);

    if (this->buffer_ == nullptr) {
        this->buffer_ = (unsigned char *) orbiter::memory::Alloc(length);

        if (this->buffer_ == nullptr)
            return false;

        this->b_wr_ = 0;
        this->b_length_ = length;
    }

    if (this->b_length_ - this->b_wr_ < length) {
        auto *tmp = (unsigned char *) orbiter::memory::Realloc(this->buffer_, this->b_length_ + length);
        if (tmp == nullptr)
            return false;

        this->buffer_ = tmp;
        this->b_length_ += length;
    }

    orbiter::memory::MemoryCopy(this->buffer_ + this->b_wr_, buffer, length);
    this->b_wr_ += length;

    return true;
}

char *InputBuffer::GetCurrentLine(int *out_len) const {
    char *line;

    size_t length;

    if (out_len != nullptr)
        *out_len = 0;

    if (this->last_line_ == nullptr) {
        length = this->b_cur_ - this->ll_end_;

        if (this->buffer_[this->ll_end_] == '\n')
            length--;

        if (length == 0)
            return nullptr;

        if (out_len != nullptr)
            *out_len = (int) length;

        line = (char *) orbiter::memory::Alloc(length + 1);
        if (line == nullptr)
            return nullptr;

        memcpy(line, this->buffer_ + this->ll_end_, length);

        line[length] = '\0';

        return line;
    }

    length = (this->ll_size_ - this->ll_end_) + this->ll_cur_;

    if (length == 0)
        return nullptr;

    if (out_len != nullptr)
        *out_len = (int) length;

    line = (char *) orbiter::memory::Alloc(length + 1);
    if (line == nullptr)
        return nullptr;

    auto head_len = this->ll_size_ - this->ll_end_;

    memcpy(line, this->last_line_ + this->ll_end_, head_len);
    memcpy(line + head_len, this->last_line_, this->ll_cur_);

    line[head_len + this->ll_cur_] = '\0';

    return line;
}

int InputBuffer::Peek(bool advance) {
    if (this->b_cur_ < this->b_length_ && this->buffer_ != nullptr) {
        auto chr = this->buffer_[this->b_cur_];

        if (advance) {
            if (this->last_line_ == nullptr) {
                if (chr == '\n')
                    this->ll_end_ = this->b_cur_ + 1;
            } else {
                this->last_line_[this->ll_cur_] = chr;

                if (chr == '\n') {
                    this->ll_cur_ = 0;
                    this->ll_end_ = this->ll_size_;
                } else {
                    if (this->ll_cur_ == this->ll_end_)
                        this->ll_end_++;

                    this->ll_cur_++;
                }

                if (this->ll_cur_ >= this->ll_size_) {
                    if (this->ll_end_ == this->ll_size_)
                        this->ll_end_ = this->ll_size_ / 2;

                    this->ll_cur_ = 0;
                }
            }

            this->b_cur_++;
        }

        return chr;
    }

    return 0;
}

int InputBuffer::ReadFile(FILE *fd) {
    assert(this->file_);

    if (this->buffer_ == nullptr) {
        this->buffer_ = (unsigned char *) orbiter::memory::Alloc(this->b_length_);

        if (this->buffer_ == nullptr)
            return false;

        this->b_cur_ = this->b_length_;

        this->last_line_ = (unsigned char *) orbiter::memory::Alloc(this->ll_size_);
        if (this->last_line_ == nullptr)
            return false;

        this->ll_end_ = this->ll_size_;
    }

    if (this->b_length_ - this->b_cur_ != 0)
        return 0;

    auto read = (int) std::fread(this->buffer_, 1, this->b_length_, fd);

    if (ferror(fd) != 0 || read == 0 && feof(fd) != 0)
        return 0;

    this->b_length_ = read;
    this->b_cur_ = 0;

    return read;
}
