// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_OBJECT_H_
#define ORBIT_LIFTOFF_IR_OBJECT_H_

namespace liftoff::ir {
    enum class ObjectType {
        BASIC_BLOCK,
        INSTRUCTION,
        MODULE,
        REGISTER,
        VALUE,
        VIRT_INSTRUCTION
    };

    class Use {
    public:
        class Object *value = nullptr;
        Object *user = nullptr;

        int index = 0;

        Use *next = nullptr;
    };

    class Object {
        struct {
            Object *next = nullptr;
            Object *prev = nullptr;
        } memory_;

        friend class Builder;

    protected:
        const ObjectType objType_;

        void AddUse(Use *u) noexcept {
            u->next = this->use_list;
            this->use_list = u;
        }

        void DeleteUse(Use *u) noexcept {
            if (this->use_list == u) {
                this->use_list = nullptr;

                return;
            }

            auto *prev = this->use_list;
            for (auto cur = this->use_list->next; cur != nullptr; cur = cur->next) {
                if (cur == u) {
                    prev->next = cur->next;

                    break;
                }

                prev = cur;
            }
        }

        void SetOperand(int operand, Object *object) noexcept {
            if (this->operands[operand].value != nullptr)
                this->DeleteUse(this->operands + operand);

            this->operands[operand].value = object;
            object->AddUse(this->operands + operand);
        }

        explicit Object(ObjectType type, int operands) noexcept: objType_(type), num_ops(operands) {
            if (operands > 0) {
                this->operands = new Use[operands];

                for (int i = 0; i < this->num_ops; ++i) {
                    this->operands[i].user = this;
                    this->operands[i].index = i;
                }
            }
        }

        explicit Object(ObjectType type) : Object(type, 0) {
        }

    public:
        Use *operands = nullptr;
        Use *use_list = nullptr;

        const U32 num_ops = 0;

        virtual ~Object() {
            delete[] this->operands;
        }

        [[nodiscard]] ObjectType type() const noexcept { return objType_; }

        void ReplaceOperand(const Object *old, Object *new_value) noexcept {
            for (int i = 0; i < this->num_ops; ++i) {
                if (this->operands[i].value == old) {
                    this->SetOperand(i, new_value);
                    break;
                }
            }
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_OBJECT_H_
