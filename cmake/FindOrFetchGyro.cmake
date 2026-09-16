# Provide the gyro::gyro target.
#
# Resolution order:
#   1. an installed gyro package (find_package);
#   2. a local checkout, via FETCHCONTENT_SOURCE_DIR_GYRO, or auto-detected
#      as the sibling directory ../gyro, so that during co-development edits
#     in gyro rebuild Orbit with no install step in between;
#   3. a fetch of ORBIT_GYRO_GIT_REPOSITORY at ORBIT_GYRO_GIT_TAG.
#
# gyro is consumed as a static, position-independent library folded into
# libOrbiter: one artifact to ship, no extra shared library.

find_package(gyro CONFIG QUIET)

if (gyro_FOUND)
    message(STATUS "gyro: using installed package ${gyro_VERSION}")
    return()
endif ()

include(FetchContent)

set(ORBIT_GYRO_GIT_REPOSITORY "https://github.com/orbitlang/gyro.git"
        CACHE STRING "gyro git repository (used when no package/local checkout is available)")

# TODO: pin to a release tag or SHA once gyro is tagged.
set(ORBIT_GYRO_GIT_TAG "main" CACHE STRING "gyro git revision to fetch")

# Co-development convenience: a sibling checkout wins over fetching.
set(_orbit_gyro_sibling "${PROJECT_SOURCE_DIR}/../gyro")
if (NOT DEFINED FETCHCONTENT_SOURCE_DIR_GYRO AND EXISTS "${_orbit_gyro_sibling}/CMakeLists.txt")
    get_filename_component(_orbit_gyro_sibling "${_orbit_gyro_sibling}" ABSOLUTE)
    set(FETCHCONTENT_SOURCE_DIR_GYRO "${_orbit_gyro_sibling}" CACHE PATH "Local gyro checkout used instead of fetching")
endif ()
unset(_orbit_gyro_sibling)

if (DEFINED FETCHCONTENT_SOURCE_DIR_GYRO)
    message(STATUS "gyro: using local checkout ${FETCHCONTENT_SOURCE_DIR_GYRO}")
else ()
    message(STATUS "gyro: fetching ${ORBIT_GYRO_GIT_REPOSITORY} @ ${ORBIT_GYRO_GIT_TAG}")
endif ()

FetchContent_Declare(gyro
        GIT_REPOSITORY ${ORBIT_GYRO_GIT_REPOSITORY}
        GIT_TAG ${ORBIT_GYRO_GIT_TAG}
        GIT_SHALLOW TRUE)

# Static + PIC so it can be folded into the shared libOrbiter. gyro already
# turns its own tests/install off when it is not the top-level project.
set(GYRO_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(gyro)
