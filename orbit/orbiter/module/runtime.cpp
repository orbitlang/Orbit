// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/config.h>
#include <orbit/orbiter/runtime.h>
#include <orbit/orbiter/version.h>

#include <orbit/orbiter/support/process.h>

#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/rawptr.h>

#include <orbit/orbiter/module/modules.h>

using namespace orbiter::datatype;
using namespace orbiter::module;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool SetAbout(orbiter::Isolate *isolate, const TypeInfo *type) {
#define ADD_VERSION(name, _value)                                                   \
    do {                                                                            \
        auto *prop = TIFindLocalProperty(type, name);                               \
        if (prop == nullptr) return false;                                          \
                                                                                    \
        auto str = ORStringIntern(isolate, _value);                                 \
        if (!str) return false;                                                     \
                                                                                    \
        prop->value = (OObject *) str.release();                                    \
    } while(0);

    ADD_VERSION("version", OR_VERSION);
    ADD_VERSION("version_ex", OR_VERSION_EX);
    ADD_VERSION("version_level", OR_RELEASE_LEVEL);

    return true;
#undef ADD_VERSION
}

bool SetExecutableName(orbiter::Isolate *isolate, const TypeInfo *type) {
    auto *prop = TIFindLocalProperty(type, "executable");
    if (prop == nullptr)
        return false;

    auto name = orbiter::support::GetExecutableName(isolate);
    if (!name)
        return false;

    prop->value = (OObject *) name.release();

    return true;
}

bool SetOsName(orbiter::Isolate *isolate, const TypeInfo *type) {
    auto *prop = TIFindLocalProperty(type, "os");
    if (prop == nullptr)
        return false;

    auto os_name = ORStringIntern(isolate, _ORBIT_PLATFORM_NAME);
    if (!os_name)
        return false;

    prop->value = (OObject *) os_name.release();

    return true;
}

static bool RuntimeInit(Module *self) {
    auto *isolate = O_GET_ISOLATE(self);
    const auto *type = O_GET_TYPE(self);

    if (!SetAbout(isolate, type))
        return false;

    if (!SetExecutableName(isolate, type))
        return false;

    if (!SetOsName(isolate, type))
        return false;

    return true;
}

// *********************************************************************************************************************
// PRIMITIVES
// *********************************************************************************************************************

RUNTIME_FUNCTION(runtime_get_argv, get_argv,
                 R"DOC(
@brief Wrap the interpreter's argv (`char **`) in a RawPtr.

@return A RawPtr holding the address of the argv array.

@panic OOMError  When the RawPtr wrapper cannot be allocated.

@see get_config
)DOC", 0, nullptr, false, false) {
    auto *isolate = O_GET_ISOLATE(_func);
    const auto *conf = isolate->config;

    return HOObject(RawPtrNew(isolate, conf->argv));
}

RUNTIME_FUNCTION(runtime_get_config, get_config,
                 R"DOC(
@brief Return the active interpreter configuration as a Dict.

Snapshots the running isolate's `Config` into a fresh Dict for runtime
inspection.

The returned Dict is a copy: mutating it does not change the live
configuration.

@return A new Dict mapping each configuration field to its current value.

@example
    let c = runtime.get_config()
    io.print(c["vc_max"])
)DOC", 0, nullptr, false, false) {
    auto *isolate = O_GET_ISOLATE(_func);
    const auto *conf = isolate->config;

    auto ret = DictNew(isolate);
    if (!ret)
        return {};

    if (conf == nullptr)
        return HOObject(std::move(ret));

#define PUT_BOOL(name, field)                                                    \
    if (!DictInsert(ret.get(), #name, (OObject *) BOOL_TO_OBOOL(field)))         \
        return {};

#define PUT_INT(name, field)                                                     \
    do {                                                                         \
        auto _v = IntNew(isolate, (IntegerUnderlying) (field));                  \
        if (!_v || !DictInsert(ret.get(), #name, (OObject *) _v.get()))          \
            return {};                                                           \
    } while (0)

    PUT_BOOL(interactive, conf->interactive);
    PUT_BOOL(quiet, conf->quiet);

    PUT_INT(argc, conf->argc);
    PUT_INT(file, conf->file);
    PUT_INT(cmd, conf->cmd);
    PUT_INT(ost_max, conf->ost_max);
    PUT_INT(vc_max, conf->vc_max);
    PUT_INT(fiber_ssize, conf->fiber_ssize);
    PUT_INT(fiber_pool, conf->fiber_pool);

#undef PUT_BOOL
#undef PUT_INT

    return HOObject(std::move(ret));
}

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

const ModuleEntry runtime_entries[] = {
    ORBIT_MODULE_EXPORT_FUNCTION(runtime_get_argv),
    ORBIT_MODULE_EXPORT_FUNCTION(runtime_get_config),

    ORBIT_MODULE_EXPORT_ALIAS("os", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("executable", nullptr),

    ORBIT_MODULE_EXPORT_ALIAS("version", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("version_ex", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("version_level", nullptr),

    ORBIT_MODULE_SENTINEL
};

ModuleInit ModuleRuntime = {
    "::orbit::runtime",
    "@brief Runtime environment introspection."
    "\n\n"
    "Provides access to runtime environment information: active configuration, "
    "command-line arguments, host platform (OS), and version information.",
    "1.0.0",
    runtime_entries,
    RuntimeInit,
    nullptr
};

const ModuleInit *orbiter::module::module_runtime_ = &ModuleRuntime;
