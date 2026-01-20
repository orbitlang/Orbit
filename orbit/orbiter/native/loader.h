// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_NATIVE_LOADER_H_
#define ORBIT_ORBITER_NATIVE_LOADER_H_

#include <mutex>

#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/nativefunc.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::native {
    class Loader {
        std::mutex cache_mutex_;
        std::mutex lib_cache_mutex_;

        datatype::HDict cache_;
        datatype::HDict lib_cache_;

        Isolate *isolate_;

        DLHandle LoadLibrary(datatype::ORString *name, bool &closable);

        datatype::HOObject LoadFunction(NativeBinding *binding);

        datatype::HOObject LoadVariable(NativeBinding *binding);

        datatype::HORString MakeKey(const datatype::ORString *library, const datatype::ORString *symbol,
                                    NativeBindingType type) const;

        void CloseLibrary(DLHandle handle, datatype::ORString *name, bool closable) const;

    public:
        explicit Loader(Isolate *isolate) : isolate_(isolate) {
        }

        bool Initialize();

        datatype::HOObject Load(NativeBinding *binding);
    };
} // orbiter::native

#endif // !ORBIT_ORBITER_NATIVE_LOADER_H_
