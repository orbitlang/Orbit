// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_VALUE_H_
#define ORBIT_LIFTOFF_IR_VALUE_H_

#include <orbit/datatype.h>

#include <orbit/liftoff/ir/object.h>

namespace liftoff::ir {
    class Value : public Object {
        friend class Builder;

    public:
        U16 value = 0;

    protected:
        explicit Value(U16 value) noexcept: Object(ObjectType::VALUE), value(value) {
        }
    };
};

#endif // !ORBIT_LIFTOFF_IR_VALUE_H_
