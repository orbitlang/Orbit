add_library(Orbiter SHARED)

file(GLOB_RECURSE VM_SRC RELATIVE "${CMAKE_CURRENT_LIST_DIR}" "*.cpp")
message(STATUS "Orbiter source files: ${VM_SRC}")

target_sources(Orbiter PRIVATE ${VM_SRC})

#configure_file("${CMAKE_CURRENT_SOURCE_DIR}/vm/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/vm/version.h")

target_include_directories(Orbiter PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

if (NOT MSVC)
    target_compile_options(Orbiter PRIVATE -fPIC)
endif ()

#set_target_properties(Orbiter PROPERTIES OUTPUT_NAME "Orbit")
set_target_properties(Orbiter PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(Orbiter PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})

# *** Options ***

option(ORBIT_FF_CGOTO "Compile Orbit using the computed goto extension" on)

if(NOT MSVC AND ORBIT_FF_CGOTO)
    target_compile_definitions(Orbiter PRIVATE ORBIT_FF_COMPUTED_GOTO)
endif()
