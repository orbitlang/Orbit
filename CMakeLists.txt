cmake_minimum_required(VERSION 3.20)

project(Orbit
        VERSION 0.3.0
        DESCRIPTION "The Orbit programming language"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(ORBIT_RELEASE_LEVEL "alpha")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ORBIT_BUILD_TESTS "Enable CTest and register the .orb suites (ortest, poc)" ${PROJECT_IS_TOP_LEVEL})
option(ORBIT_BUILD_UNIT_TESTS "Build the GoogleTest unit-test executable" OFF)
option(ORBIT_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})
option(ORBIT_FF_CGOTO "Compile the VM dispatch loop with the computed-goto extension" OFF)
option(ORBIT_WERROR "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Global configuration; when Orbit is the top-level project, so that a
# consumer embedding Orbit keeps control of its own build settings.
# ---------------------------------------------------------------------------
if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    # Strict ISO C++ (-std=c++17) by default, so accidental GNU-isms are caught.
    # The one deliberate GNU extension (the computed-goto dispatch loop) is
    # re-enabled per-target on Orbiter only when ORBIT_FF_CGOTO is ON.
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Keep all artifacts in one place regardless of generator.
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
    endif ()
endif ()

find_package(Threads REQUIRED)

# gyro::gyro: The async event loop / I/O runtime (installed package, or a
# pinned fetch, or a sibling checkout during co-development).
include(FindOrFetchGyro)

add_subdirectory(lib/stratum)
add_subdirectory(orbit)

# ---------------------------------------------------------------------------
# Orbit: the command-line driver
# ---------------------------------------------------------------------------
add_executable(Orbit orbit/main.cpp)
add_executable(Orbit::Orbit ALIAS Orbit)

target_link_libraries(Orbit PRIVATE Orbit::Orbiter)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if (ORBIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif ()

# ---------------------------------------------------------------------------
# Install / package
# ---------------------------------------------------------------------------
if (ORBIT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS Orbit Orbiter
            EXPORT OrbitTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # Public headers. The include root is the repository root (#include <orbit/...>),
    # and the public API also includes <stratum/...>.
    install(DIRECTORY orbit/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/orbit
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

    install(DIRECTORY lib/stratum/stratum/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/stratum
            FILES_MATCHING PATTERN "*.h")

    install(FILES "${PROJECT_BINARY_DIR}/include/orbit/orbiter/version.h"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/orbit/orbiter)

    install(EXPORT OrbitTargets
            FILE OrbitTargets.cmake
            NAMESPACE Orbit::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orbit)

    configure_package_config_file(
            "${PROJECT_SOURCE_DIR}/cmake/OrbitConfig.cmake.in"
            "${PROJECT_BINARY_DIR}/OrbitConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orbit)

    write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/OrbitConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)

    install(FILES
            "${PROJECT_BINARY_DIR}/OrbitConfig.cmake"
            "${PROJECT_BINARY_DIR}/OrbitConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orbit)
endif ()
