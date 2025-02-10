// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_IALLOCATOR_H_
#define ORBIT_ORBITER_MEMORY_IALLOCATOR_H_

#include <orbit/orbiter/memory/memory.h>

namespace orbiter {
    class Isolate;

    namespace memory {
        class IsolateAllocator {
            Isolate *isolate_;

            stratum::Memory *allocator_;

        public:
            using size_type = MSize;

            explicit IsolateAllocator(Isolate *isolate) noexcept;

            [[nodiscard]] Isolate *GetIsolate() const noexcept {
                return isolate_;
            }

            template<typename T, typename... Args>
            [[nodiscard]] T *AllocObject(Args... args) {
                auto mem = this->alloc<T>(sizeof(T));
                T *obj = nullptr;

                if (mem != nullptr) {
                    try {
                        obj = new(mem) T(args...);
                    } catch (...) {
                        this->free(mem);
                        throw;
                    }
                }

                return obj;
            }

            template<typename T>
            [[nodiscard]] T *alloc(size_type size) noexcept {
                return static_cast<T *>(this->allocator_->Alloc(size));
            }

            template<typename T>
            [[nodiscard]] T *calloc(size_type size) noexcept {
                auto *tmp = static_cast<T *>(this->allocator_->Calloc(size));
                if (!tmp)
                    memory::MemoryZero(tmp, size);

                return tmp;
            }

            template<typename T>
            [[nodiscard]] T *realloc(T *ptr, size_type size) noexcept {
                return static_cast<T *>(this->allocator_->Realloc(ptr, size));
            }

            void free(void *ptr) const noexcept {
                this->allocator_->Free(ptr);
            }

            template<typename T>
            void FreeObject(T *obj) {
                obj->~T();
                this->free(obj);
            }
        };
    }
}

#endif // !ORBIT_ORBITER_MEMORY_IALLOCATOR_H_
