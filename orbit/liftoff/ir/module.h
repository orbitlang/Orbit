// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_MODULE_H_
#define ORBIT_LIFTOFF_IR_MODULE_H_

#include <orbit/liftoff/ir/ircontext.h>
#include <orbit/liftoff/ir/object.h>

namespace liftoff::ir {
    class Module : public Object {
        IRContext context_;

        Module() : Object(ObjectType::MODULE), context_(IRContextType::MODULE) {
        }

        friend class Builder;
    };
}

#endif // !ORBIT_LIFTOFF_IR_MODULE_H_
