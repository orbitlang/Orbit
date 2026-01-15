add_library(Orbiter SHARED)

file(GLOB_RECURSE VM_SRC RELATIVE "${CMAKE_CURRENT_LIST_DIR}" "*.cpp")
message(STATUS "Orbiter source files: ${VM_SRC}")

# Add architecture-specific assembly files
enable_language(ASM)
set(ASM_SRC "")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    file(GLOB ASM_SRC_ARCH "${CMAKE_CURRENT_SOURCE_DIR}/orbiter/native/arch/arm64/*.S")
    set(ASM_SRC ${ASM_SRC_ARCH})
    message(STATUS "Using ARM64 assembly files")

    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    file(GLOB ASM_SRC_ARCH "${CMAKE_CURRENT_SOURCE_DIR}/orbiter/native/arch/x86_64/*.S")
    set(ASM_SRC ${ASM_SRC_ARCH})
    message(STATUS "Using x86_64 assembly files")

    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
endif()

if(ASM_SRC)
    message(STATUS "Assembly files: ${ASM_SRC}")
endif()

target_sources(Orbiter PRIVATE ${VM_SRC} ${ASM_SRC})

#configure_file("${CMAKE_CURRENT_SOURCE_DIR}/vm/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/vm/version.h")

target_include_directories(Orbiter PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

if(WIN32)
    target_link_libraries(Orbiter PUBLIC Stratum synchronization ws2_32)
else ()
    target_link_libraries(Orbiter PUBLIC Stratum)
endif()

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
