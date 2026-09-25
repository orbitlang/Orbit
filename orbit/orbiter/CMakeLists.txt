# orbiter: the VM (object model, memory/GC, fibers, native FFI, builtin modules).
#
# Platform-specific units (winerror, support/nt, native/w64call, native/ucall)
# guard themselves internally and are listed unconditionally.

target_sources(Orbiter PRIVATE
        config.cpp
        evloop.cpp
        fiber.cpp
        fqueue.cpp
        isolate.cpp
        orbcall.cpp
        orbit.cpp
        panic.cpp
        runtime.cpp
        vm.cpp
        vmstack.cpp

        # Object model
        datatype/atom.cpp
        datatype/bytes.cpp
        datatype/byteview.cpp
        datatype/chan.cpp
        datatype/closure.cpp
        datatype/code.cpp
        datatype/context.cpp
        datatype/ctbuilder.cpp
        datatype/decimal.cpp
        datatype/dict.cpp
        datatype/error.cpp
        datatype/function.cpp
        datatype/future.cpp
        datatype/generator.cpp
        datatype/iterator.cpp
        datatype/list.cpp
        datatype/module.cpp
        datatype/nativefunc.cpp
        datatype/number.cpp
        datatype/oobject.cpp
        datatype/oops.cpp
        datatype/orstring.cpp
        datatype/pcheck.cpp
        datatype/rawptr.cpp
        datatype/result.cpp
        datatype/rguard.cpp
        datatype/set.cpp
        datatype/stringbuilder.cpp
        datatype/stringformatter.cpp
        datatype/tuple.cpp
        datatype/type.cpp
        datatype/winerror.cpp
        datatype/c3/c3.cpp
        datatype/support/shared_buffer.cpp
        datatype/support/slice.cpp

        # Import system
        import/importer.cpp
        import/importspec.cpp
        import/locator.cpp
        import/module_entry.cpp

        # Memory / GC
        memory/gc.cpp
        memory/iallocator.cpp

        # Builtin native modules
        module/builtin.cpp
        module/chrono.cpp
        module/ffi.cpp
        module/gc.cpp
        module/io.cpp
        module/runtime.cpp
        module/net.cpp

        # Native FFI
        native/dlwrap.cpp
        native/loader.cpp
        native/prepare.cpp
        native/ucall.cpp
        native/w64call.cpp

        # Platform support / synchronization
        support/process.cpp
        support/nt/nt.cpp
        sync/asyncrwlock.cpp
        sync/monitor.cpp)
