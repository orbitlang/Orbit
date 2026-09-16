# ---------------------------------------------------------------------------
# Language-level suites (.orb): the real regression gates.
#
# They only need the built `Orbit` binary, so `ctest` runs them next to any
# unit tests: ortest (feature/acceptance) and issues/poc (bug reproducers).
# The runners locate the binary through ORBIT_BUILD_DIR.
# ---------------------------------------------------------------------------
add_test(NAME ortest
        COMMAND "${PROJECT_SOURCE_DIR}/ortest/run.sh"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_test(NAME poc
        COMMAND "${PROJECT_SOURCE_DIR}/issues/poc/run.sh"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

set_tests_properties(ortest poc PROPERTIES
        ENVIRONMENT "ORBIT_BUILD_DIR=${PROJECT_BINARY_DIR}"
        LABELS "orb")

# ---------------------------------------------------------------------------
# GoogleTest unit tests: scaffolded, off by default (no valid suites yet).
# Enable with -DORBIT_BUILD_UNIT_TESTS=ON once the cases are brought back.
# ---------------------------------------------------------------------------
if (ORBIT_BUILD_UNIT_TESTS)
    include(FetchContent)

    # Use a system/vcpkg GoogleTest when available, otherwise fetch it.
    find_package(GTest QUIET)

    if (NOT GTest_FOUND)
        FetchContent_Declare(googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.17.0
                GIT_SHALLOW TRUE
                EXCLUDE_FROM_ALL)

        # Keep MSVC runtimes consistent with the consuming project.
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(googletest)
    endif ()

    include(GoogleTest)

    add_executable(orbit_unit_tests
            main.cpp
            util/hashmap.cpp
            liftoff/symtable.cpp
            liftoff/scanner/ibuffer.cpp
            liftoff/scanner/scanner.cpp
            liftoff/scanner/sbuffer.cpp)

    # main.cpp provides its own main(): link gtest, not gtest_main.
    target_link_libraries(orbit_unit_tests PRIVATE Orbit::Orbiter GTest::gtest)

    if (MSVC)
        target_compile_options(orbit_unit_tests PRIVATE /W4)
    else ()
        target_compile_options(orbit_unit_tests PRIVATE -Wall -Wextra)
    endif ()

    gtest_discover_tests(orbit_unit_tests
            DISCOVERY_TIMEOUT 30
            DISCOVERY_MODE PRE_TEST
            PROPERTIES LABELS unit)
endif ()
