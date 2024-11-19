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
        VALUE
    };

    class Object {
    protected:
        const ObjectType objType_;

        explicit Object(ObjectType type) noexcept: objType_(type) {
        }

    public:
        [[nodiscard]] ObjectType type() const noexcept { return objType_; }
    };
}

#endif // !ORBIT_LIFTOFF_IR_OBJECT_H_
