// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_IRCONTEXT_H_
#define ORBIT_LIFTOFF_IR_IRCONTEXT_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/datatype/list.h>

#include <orbit/liftoff/ir/basicblock.h>
#include <orbit/liftoff/ir/jblock.h>

namespace liftoff::ir {
    enum class IRContextType {
        CLOSURE,
        FUNCTION,
        METHOD,
        MODULE
    };

    class IRContext {
        Object *objs_ = nullptr;

        orbiter::Isolate *isolate_ = nullptr;

        IRContext *back = nullptr;

        orbiter::datatype::HList static_values;

        struct {
            IRContext **context = nullptr;

            U16 count = 0;
            U16 size = 0;
        } sub;

        U32 logical_counter_ = 0;

        U32 vreg_counter_ = 0;

        friend class Builder;

        explicit IRContext(orbiter::Isolate *isolate,
                           const IRContextType type) noexcept: isolate_(isolate), type_(type) {
        }

        ~IRContext() noexcept {
            if (this->sub.context != nullptr) {
                const orbiter::IsolateAllocator allocator(this->isolate_);

                for (auto i = 0; i < this->sub.count; i++)
                    Delete(this->sub.context[i]);

                allocator.free(this->sub.context);
            }
        }

        U16 PushSubContext(IRContext *context) {
            orbiter::IsolateAllocator allocator(this->isolate_);

            if (this->sub.context == nullptr) {
                this->sub.context = allocator.alloc<IRContext *>(8 * sizeof(void *));
                if (this->sub.context == nullptr)
                    throw std::bad_alloc();

                this->sub.count = 0;
                this->sub.size = 8;
            }

            if (this->sub.count + 1 >= this->sub.size) {
                const auto tmp = allocator.realloc<IRContext *>(this->sub.context,
                                                                (this->sub.size + 8) * sizeof(void *));
                if (tmp == nullptr)
                    throw std::bad_alloc();

                this->sub.context = tmp;
                this->sub.size += 8;
            }

            context->back = this;
            this->sub.context[this->sub.count] = context;

            return this->sub.count++;
        }

    public:
        BasicBlock *entry_ = nullptr;
        BasicBlock *current_ = nullptr;

        JBlock *j_chain = nullptr;

        IRContextType type_;

        I32 GetIncRVirtCounter() noexcept {
            return (I32) this->vreg_counter_++;
        }

        U16 PushStaticValue(orbiter::datatype::OObject *value) {
            if (!this->static_values) {
                this->static_values = orbiter::datatype::ListNew(this->isolate_);
                if (!this->static_values)
                    throw std::bad_alloc();
            }

            if (!ListAppend(this->static_values.get(), value))
                throw std::bad_alloc();

            return this->static_values->length - 1;
        }

        [[nodiscard]] U16 GetSubcontextCount() const noexcept {
            return this->sub.count;
        }

        static void Delete(IRContext *context) {
            if (context == nullptr)
                return;

            const orbiter::IsolateAllocator allocator(context->isolate_);

            context->~IRContext();

            allocator.free(context);
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRCONTEXT_H_
