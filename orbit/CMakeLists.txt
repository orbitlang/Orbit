# Orbiter: the Orbit runtime (the liftoff compiler and the orbiter VM) in one
# shared library. Sources are declared by each module in its own CMakeLists.

include(GNUInstallDirs)

add_library(Orbiter SHARED)
add_library(Orbit::Orbiter ALIAS Orbiter)

add_subdirectory(liftoff)
add_subdirectory(orbiter)

target_compile_features(Orbiter PUBLIC cxx_std_17)
if (MSVC)
    # errors.h relies on __VA_OPT__, which MSVC only provides in C++20 mode.
    target_compile_features(Orbiter PUBLIC cxx_std_20)
endif ()

set_target_properties(Orbiter PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME Orbiter
        # Every symbol is exported until the public API is annotated with an
        # ORBIT_API export macro (planned follow-up).
        WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ---------------------------------------------------------------------------
# version.h is generated into the build tree, never into the source tree.
# ---------------------------------------------------------------------------
set(OR_MAJOR ${PROJECT_VERSION_MAJOR})
set(OR_MINOR ${PROJECT_VERSION_MINOR})
set(OR_PATCH ${PROJECT_VERSION_PATCH})
set(OR_RELEASE_LEVEL ${ORBIT_RELEASE_LEVEL})

configure_file(orbiter/version.h.in
        "${PROJECT_BINARY_DIR}/include/orbit/orbiter/version.h" @ONLY)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(Orbiter
        PUBLIC
        # The include root is the repository root: #include <orbit/...>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        # Generated headers (version.h)
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        # Public headers include <stratum/...>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/stratum>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# ---------------------------------------------------------------------------
# Compile options
# ---------------------------------------------------------------------------
# The dispatch loop uses computed goto (a GNU extension, not ISO C++): opt the
# Orbiter target back into the GNU dialect only when it is actually compiled in.
if (ORBIT_FF_CGOTO AND NOT MSVC)
    target_compile_definitions(Orbiter PRIVATE ORBIT_FF_CGOTO)
    set_target_properties(Orbiter PROPERTIES CXX_EXTENSIONS ON)
endif ()

if (MSVC)
    target_compile_options(Orbiter PRIVATE /W4 $<$<BOOL:${ORBIT_WERROR}>:/WX>)
else ()
    target_compile_options(Orbiter PRIVATE -Wall -Wextra $<$<BOOL:${ORBIT_WERROR}>:-Werror>)
endif ()

# ---------------------------------------------------------------------------
# Architecture-specific FFI stubs
# ---------------------------------------------------------------------------
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    enable_language(ASM)
    target_sources(Orbiter PRIVATE orbiter/native/arch/arm64/preload.S)
    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if (MSVC)
        # MASM (ml64) requires the .asm extension.
        enable_language(ASM_MASM)
        target_sources(Orbiter PRIVATE
                orbiter/native/arch/x86_64/win64/ffi_call.asm
                orbiter/native/arch/x86_64/win64/preload.asm)
    else ()
        enable_language(ASM)
        target_sources(Orbiter PRIVATE orbiter/native/arch/x86_64/preload.S)
    endif ()
    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
endif ()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# Threads is public: std::mutex & co. appear in public headers (e.g. gc.h).
# stratum and gyro are implementation details folded into the library.
target_link_libraries(Orbiter
        PUBLIC Threads::Threads
        PRIVATE Stratum gyro::gyro)

if (WIN32)
    target_link_libraries(Orbiter PRIVATE synchronization ws2_32)
endif ()
