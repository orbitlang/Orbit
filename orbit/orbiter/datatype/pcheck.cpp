// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/datatype/pcheck.h>

using namespace orbiter::datatype;

bool orbiter::datatype::CheckParameter(const Parameter *parameters, Function *func, OObject **argv, const U16 argc) {
    char type_name[64];

    const auto isolate = Fiber::Current()->isolate;
    const auto is_method = func->shared->IsMethod();

    auto index = 0;

    if (is_method) {
        if (!IsTypeExtends(GetTypeInfoFromObject(isolate, argv[0]), func->shared->owner_type)) {
            ErrorSet(isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::METHOD_RECEIVER],
                     func->shared->owner_type->name);

            return false;
        }

        index += 1;
    }

    for (auto *cursor = parameters; cursor->name != nullptr; cursor++) {
        bool ok = false;

        // The call machinery pads an omitted optional with the sentinel, so a
        // short argv means this native's declared arity disagrees with its
        // parameter table: a bug in the module, not in the call. Assert in
        // debug, and report it rather than read past argv in release.
        assert(index < argc);

        if (index >= argc) {
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
            if (O_IS_OBJECT(value))
                ok = (cursor->types >> (U32) O_GET_TYPE(value)->i_type) & 1;
            else {
                if (O_IS_SMI(value) && ((cursor->types >> (U32) InstanceType::NUMBER) & 1))
                    ok = true;

                if (O_IS_ODDBALL(value) && ((cursor->types >> (U32) InstanceType::BOOLEAN) & 1))
                    ok = true;
            }
        }

        if (!ok && O_IS_SENTINEL(value)) {
            if (!cursor->optional) {
                ErrorSet(isolate,
                         ValueError::Details[ValueError::Reason::ID],
                         nullptr,
                         ValueError::Details[ValueError::Reason::MISSING_PARAMETER],
                         cursor->name,
                         index);

                return false;
            }

            ok = true;
        }

        if (!ok && cursor->types != 0) {
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
