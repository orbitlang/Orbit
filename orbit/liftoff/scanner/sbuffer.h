// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SCANNER_SBUFFER_H_
#define ORBIT_LIFTOFF_SCANNER_SBUFFER_H_

#include <orbit/orbiter/isolate.h>

namespace liftoff::scanner {
    class StoreBuffer {
        orbiter::memory::IsolateAllocator allocator_;

        unsigned char *buffer_ = nullptr;
        unsigned char *cursor_ = nullptr;
        unsigned char *end_ = nullptr;

        bool Enlarge(size_t increase);

    public:
        explicit StoreBuffer(orbiter::Isolate *isolate): allocator_(isolate) {
        }

        ~StoreBuffer();

        bool PutChar(unsigned char chr);

        bool PutCharRepeat(unsigned char chr, int n);

        bool PutString(const unsigned char *str, size_t length);

        [[nodiscard]] size_t GetLength() const {
            if (this->buffer_ == nullptr)
                return 0;

            return (size_t) (this->cursor_ - this->buffer_);
        }

        unsigned int GetBuffer(unsigned char **buffer);
    };
}

#endif // !ORBIT_LIFTOFF_SCANNER_SBUFFER_H_
