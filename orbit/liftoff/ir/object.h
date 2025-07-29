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
        friend class IRContext;

    protected:
        const ObjectType objType_;

        void AddUse(Use *u) noexcept {
            if (this->use_list == nullptr) {
                this->use_list = u;

                return;
            }

            // Preserve the order in which values are added
            auto *cursor = this->use_list;
            while (cursor->next != nullptr)
                cursor = cursor->next;

            cursor->next = u;
        }

        void DeleteUse(Use *u) noexcept {
            Use *prev = nullptr;

            for (auto cur = this->use_list; cur != nullptr; cur = cur->next) {
                if (cur == u) {
                    if (prev == nullptr) {
                        cur->next = nullptr;

                        this->use_list = nullptr;

                        return;
                    }

                    prev->next = cur->next;
                    cur->next = nullptr;

                    break;
                }

                prev = cur;
            }
        }

        void SetOperand(int operand, Object *object) const noexcept {
            if (this->operands[operand].value != nullptr)
                this->operands[operand].value->DeleteUse(this->operands + operand);

            if (object != nullptr) {
                this->operands[operand].value = object;
                object->AddUse(this->operands + operand);
            }
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
            if (this->num_ops > 0 && this->operands != nullptr) {
                for (int i = 0; i < this->num_ops; ++i) {
                    if (this->operands[i].value != nullptr)
                        this->operands[i].value->DeleteUse(this->operands + i);
                }
            }

            delete[] this->operands;
        }

        [[nodiscard]] ObjectType type() const noexcept { return objType_; }

        void ReplaceOperand(const Object *old, Object *new_value) const noexcept {
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
