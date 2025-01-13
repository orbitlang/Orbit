// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_ISOLATE_H_
#define ORBIT_ORBITER_ISOLATE_H_

#include <stratum/memory.h>

#include <orbit/orbiter/datatype/obase.h>

namespace orbiter {
    class Isolate {
        stratum::Memory allocator_{};

        template<typename T>
        friend class IsolateAllocatorBase;

    public:
        datatype::TypeInfo *primitive[datatype::kInstanceTypeCount];

        Isolate() = delete;

        static Isolate *New();
    };

    template<typename Derived>
    class IsolateAllocatorBase {
        Isolate *isolate_;

    protected:
        stratum::Memory *allocator_;

    public:
        using size_type = MSize;

        explicit IsolateAllocatorBase(Isolate *isolate) noexcept : isolate_(isolate), allocator_(&isolate->allocator_) {
        }

        [[nodiscard]] Isolate *GetIsolate() const noexcept {
            return isolate_;
        }

        template<typename T, typename... Args>
        [[nodiscard]] T *AllocObject(Args... args) {
            auto mem = static_cast<Derived *>(this)->template alloc<T>(sizeof(T));
            T *obj = nullptr;

            if (mem != nullptr) {
                try {
                    obj = new(mem) T(args...);
                } catch (...) {
                    static_cast<Derived *>(this)->free(mem);
                    throw;
                }
            }

            return obj;
        }

        template<typename T>
        [[nodiscard]] T *alloc(size_type size) noexcept {
            return static_cast<Derived *>(this)->template alloc<T>(size);
        }

        template<typename T>
        [[nodiscard]] T *calloc(size_type size) noexcept {
            return static_cast<Derived *>(this)->template calloc<T>(size);
        }

        template<typename T>
        [[nodiscard]] T *realloc(T *ptr, size_type size) noexcept {
            return static_cast<Derived *>(this)->template realloc<T>(ptr, size);
        }

        void free(void *ptr) noexcept {
            return static_cast<Derived *>(this)->free(ptr);
        }

        template<typename T>
        void FreeObject(T *obj) {
            obj->~T();
            this->free(obj);
        }
    };

    class IsolateAllocator : public IsolateAllocatorBase<IsolateAllocator> {
    public:
        explicit IsolateAllocator(Isolate *isolate) noexcept : IsolateAllocatorBase(isolate) {
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
    };
}

#endif // !ORBIT_ORBITER_ISOLATE_H_
