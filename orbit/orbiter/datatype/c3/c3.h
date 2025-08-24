// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_C3_C3_H_
#define ORBIT_ORBITER_DATATYPE_C3_C3_H_

#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/tuple.h>

namespace orbiter::datatype::linearization {
    class C3 {
        TypeInfo *type_;

        HList BuildBasesList(TypeInfo **bases, U16 length) const noexcept;

        HTuple CalculateMRO(const List *bases) const noexcept;
    public:
        explicit C3(TypeInfo *type) noexcept : type_(type) {
        }

        bool BuildMRO(TypeInfo **bases, U16 length) const noexcept;
    };
}

#endif // !ORBIT_ORBITER_DATATYPE_C3_C3_H_
