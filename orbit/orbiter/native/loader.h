// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_NATIVE_LOADER_H_
#define ORBIT_ORBITER_NATIVE_LOADER_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/nativefunc.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::native {
    class Loader {
        datatype::HDict cache_;

        Isolate *isolate_;

        datatype::HOObject LoadFunction(NativeBinding *binding) const;

        datatype::HOObject LoadVariable(NativeBinding *binding) const;

        datatype::HORString MakeKey(const datatype::ORString *library, const datatype::ORString *symbol,
                                    NativeBindingType type) const;
    public:
        explicit Loader(Isolate *isolate) : isolate_(isolate) {
        }

        bool Initialize();

        datatype::HOObject Load(NativeBinding *binding) const;
    };
} // orbiter::native

#endif // !ORBIT_ORBITER_NATIVE_LOADER_H_
