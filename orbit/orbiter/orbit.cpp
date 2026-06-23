// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <orbit/liftoff/compiler.h>
#include <orbit/orbiter/version.h>

#include <orbit/orbiter/memory/memory.h>

#include <orbit/orbiter/config.h>
#include <orbit/orbiter/panic.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/import/importer.h>

#include <orbit/orbiter/support/process.h>

#include <orbit/orbiter/datatype/module.h>

#include <orbit/orbiter/orbit.h>

using namespace orbiter;

void DieWith(const Isolate *isolate, const char *context) {
    char buf[1024];

    const int saved_errno = errno;

    if (context == nullptr)
        context = "error";

    if (isolate != nullptr && isolate->panic.HasPanic()) {
        PanicFormat(&isolate->panic, buf, sizeof(buf));

        std::fprintf(stderr, "\norbit: %s: %s\n", context, buf);
    } else
        std::fprintf(stderr, "\norbit: %s: %s\n", context, std::strerror(saved_errno));

    std::exit(EXIT_FAILURE);
}

template<typename T>
datatype::Handle<T> OrDie(const datatype::Handle<T> &handle, Isolate *isolate, const char *context) {
    if (!handle)
        DieWith(isolate, context);

    return std::move(handle);
}

bool SetupImportPath(Isolate *isolate) noexcept {
    const auto *importer = isolate->importer_;

    const auto ex_path = support::GetExecutablePath(isolate);
    if (!ex_path)
        return false;

    if (!importer->AddRoot(ex_path.get()))
        return false;

    const auto packages = import::JoinPath(isolate, ex_path.get(), "packages");
    if (!packages)
        return false;

    if (!importer->AddRoot(packages.get()))
        return false;

    // *** PATHS FROM ENVS VAR ***
    auto *ev_paths = std::getenv(kEvarPath);
    if (ev_paths == nullptr)
        return true;

    ev_paths = strdup(ev_paths);
    if (ev_paths == nullptr)
        return false;

    while (ev_paths != nullptr) {
        const auto *path = strsep(&ev_paths, _ORBIT_PLATFORM_PATHSSEP);
        if (*path == '\0')
            continue;

        if (!importer->AddRoot(path)) {
            std::free(ev_paths);

            return false;
        }
    }

    std::free(ev_paths);

    return true;
}

datatype::HContext LoadDefaultContext(Isolate *isolate, const bool with_builtins) noexcept {
    auto context = datatype::ContextNew(isolate);
    if (!context)
        return {};

    if (with_builtins) {
        auto *importer = isolate->importer_;

        const auto *builtin = importer->Import("::orbit::builtin", nullptr);
        if (!builtin)
            return {};

        if (!datatype::ContextImportFromModule(context.get(), builtin))
            return {};
    }

    return context;
}

void EvalCommand(Isolate *isolate, datatype::Context *context, const char *command,
                 const liftoff::OptimizationLevel level) noexcept {
    liftoff::Compiler compiler(isolate, level, false);
    liftoff::scanner::Scanner scanner(isolate, command);

    const auto code = OrDie(compiler.Compile("", scanner), isolate, "compile");

    const auto result = Orbiter::GetInstance()->Eval(context, nullptr, code.get());
    if (!result && isolate->panic.HasPanic())
        DieWith(isolate, "eval");
}

void EvalFile(Isolate *isolate, datatype::Context *context, const char *name, const char *path,
              const liftoff::OptimizationLevel level) noexcept {
    liftoff::Compiler compiler(isolate, level, true);

    auto *file = fopen(path, "r");
    if (file == nullptr)
        DieWith(isolate, path);

    const auto code = OrDie(compiler.Compile(name, file), isolate, "compile");

    fclose(file);

    const auto mod_name = OrDie(datatype::ORStringIntern(isolate, name), isolate, "intern module name");
    const auto mod_type = OrDie(datatype::ModuleTypeNew(code.get(), mod_name.get()), isolate, "create module type");
    const auto module = OrDie(datatype::ModuleNew(mod_type.get()), isolate, "create module instance");

    const auto result = Orbiter::GetInstance()->Eval(context, module.get(), code.get());
    if (!result && isolate->panic.HasPanic())
        DieWith(isolate, "eval");
}

int orbiter::main(const int argc, char **argv) {
    Config config{};

    memory::MemoryCopy(&config, kConfigDefault, sizeof(Config));

    if (!ConfigInit(&config, argc, argv))
        return EXIT_FAILURE;

    if (!Orbiter::Initialize(&config))
        return EXIT_FAILURE;

    auto *isolate = Isolate::New(&config);
    if (isolate == nullptr)
        return EXIT_FAILURE;

    if (!SetupImportPath(isolate))
        DieWith(isolate, "setup import path");

    const auto context = OrDie(LoadDefaultContext(isolate, true), isolate, "load default context");

    if (config.file > -1)
        EvalFile(isolate, context.get(), "__main__", argv[config.file], liftoff::kDefaultOptimization);

    if (config.cmd > -1)
        EvalCommand(isolate, context.get(), argv[config.cmd], liftoff::kDefaultOptimization);

    if (config.interactive) {
        if (!config.quiet)
            printf("%s\n", OR_VERSION_EX);

        EvalCommand(isolate, context.get(), "import \"repl\"; repl.default_session.run()",
                    liftoff::kDefaultOptimization);
    }

    Orbiter::GetInstance()->Finalize();

    return EXIT_SUCCESS;
}
