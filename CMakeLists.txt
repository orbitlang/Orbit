cmake_minimum_required(VERSION 3.15)
project(Orbit VERSION 0.1.0)

set(OR_MAJOR ${PROJECT_VERSION_MAJOR})
set(OR_MINOR ${PROJECT_VERSION_MINOR})
set(OR_PATCH ${PROJECT_VERSION_PATCH})
set(OR_RELEASE_LEVEL "alpha")

set(BASE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(BASE_OUTPUT_HEADERS ${BASE_OUTPUT_DIRECTORY}/include/Orbit)

if (${CMAKE_BUILD_TYPE} MATCHES "Release")
    set(BASE_OUTPUT_DIRECTORY "${BASE_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}")
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BASE_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BASE_OUTPUT_DIRECTORY})

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
    set(CMAKE_CXX_STANDARD 20)
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif ()

add_subdirectory(lib/stratum)
add_subdirectory(orbit)
add_subdirectory(test EXCLUDE_FROM_ALL)

# *** Orbit main executable ***

add_executable(${CMAKE_PROJECT_NAME} "${CMAKE_SOURCE_DIR}/orbit/main.cpp")
add_dependencies(${CMAKE_PROJECT_NAME} Orbiter)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Orbiter)

# *** Install ***

set(PROJECT_NAME_MAJOR_MINOR ${PROJECT_NAME}${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
set(PROJECT_NAME_OUTPUT_LIB "orbit${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

# *** Export Orbit headers file ***

file(GLOB_RECURSE HEADERS
        "${PROJECT_SOURCE_DIR}/orbit/*.h"
        "${PROJECT_SOURCE_DIR}/orbit/*.hpp")

file(MAKE_DIRECTORY ${BASE_OUTPUT_HEADERS})

foreach (header_file ${HEADERS})
    file(RELATIVE_PATH rel_header_file ${PROJECT_SOURCE_DIR}/orbit ${header_file})
    get_filename_component(header_path "${rel_header_file}" DIRECTORY)
    file(MAKE_DIRECTORY ${BASE_OUTPUT_HEADERS}/${header_path})
    configure_file(${header_file} ${BASE_OUTPUT_HEADERS}/${rel_header_file} COPYONLY)
endforeach ()

install(TARGETS ${CMAKE_PROJECT_NAME} Orbiter
        EXPORT OrbitTargets)

# Install headers
install(DIRECTORY ${BASE_OUTPUT_HEADERS} DESTINATION include)

install(EXPORT OrbitTargets
        FILE OrbitTargets.cmake
        NAMESPACE Orbit::
        DESTINATION lib/cmake/${PROJECT_NAME_MAJOR_MINOR})

include(CMakePackageConfigHelpers)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/OrbitConfig.cmake"
        INSTALL_DESTINATION "lib/cmake/${PROJECT_NAME_MAJOR_MINOR}")

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/OrbitConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY AnyNewerVersion)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/OrbitConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/OrbitConfigVersion.cmake
        DESTINATION lib/cmake/${PROJECT_NAME_MAJOR_MINOR})

# *** Export package ***

set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)

export(TARGETS Orbiter
        NAMESPACE Orbit::
        FILE "${CMAKE_CURRENT_BINARY_DIR}/OrbitTargets.cmake")

export(PACKAGE Orbit)
