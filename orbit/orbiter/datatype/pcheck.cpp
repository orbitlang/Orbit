// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/datatype/pcheck.h>

using namespace orbiter::datatype;

bool orbiter::datatype::CheckParameter(const Parameter *parameters, OObject **argv, const U16 argc) {
    char type_name[64];

    const auto isolate = Fiber::Current()->isolate;

    auto index = 0;

    for (auto *cursor = parameters; cursor->name != nullptr; cursor++) {
        bool ok = false;

        if (index >= argc && !cursor->optional) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     ValueError::Details[ValueError::Reason::MISSING_PARAMETER],
                     cursor->name,
                     index);

            return false;
        }

        const auto *value = argv[index];

        if (value != nullptr) {
            if (O_IS_OBJECT(value)) {
                if ((cursor->types >> (U32) O_GET_TYPE(value)->i_type) & 1)
                    ok = true;
            } else {
                if ((cursor->types >> (U32) InstanceType::NUMBER) & 1) {
                    if (O_IS_SMI(value))
                        ok = true;
                }

                if ((cursor->types >> (U32) InstanceType::BOOLEAN) & 1) {
                    if (O_IS_ODDBALL(value))
                        ok = true;
                }
            }
        } else if (cursor->optional)
            ok = true;

        if (!ok) {
            GetTypeName(isolate, value, type_name, sizeof(type_name));
            ErrorSet(isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::PARAMETER],
                     type_name,
                     cursor->name,
                     index);

            return false;
        }

        index += 1;
    }

    return true;
}
