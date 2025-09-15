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

        if (index >= argc)
            assert(false); // Never get here

        const auto *value = argv[index];

        if (value != nullptr) {
            for (const auto type: cursor->types) {
                if (O_IS_OBJECT(value)) {
                    if (O_IS_TYPE(value, type)) {
                        ok = true;

                        break;
                    }
                } else {
                    if (type == InstanceType::NUMBER) {
                        if (O_IS_SMI(value)) {
                            ok = true;

                            break;
                        }
                    }

                    if (type == InstanceType::BOOLEAN) {
                        if (O_IS_ODDBALL(value)) {
                            ok = true;

                            break;
                        }
                    }
                }
            }
        } else {
            ok = true;

            if (!cursor->optional) {
                GetTypeName(value, type_name, sizeof(type_name));
                ErrorSet(isolate,
                         ValueError::Details[ValueError::Reason::ID],
                         nullptr,
                         ValueError::Details[ValueError::Reason::PARAMETER],
                         type_name,
                         cursor->name,
                         index);

                return false;
            }
        }

        if (!ok) {
            GetTypeName(value, type_name, sizeof(type_name));
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
