# stratum: low-level memory utilities (arena, memory, memutil, osmemory).
#
# Vendored and slated for removal. It is built as an object library and folded
# into libOrbiter: every stratum symbol ends up in the runtime, no separate
# library is produced or shipped. Its headers are part of Orbit's public API
# (<stratum/...>), so the include directory is re-exported by the Orbiter target.

add_library(Stratum OBJECT
        stratum/arena.cpp
        stratum/memory.cpp
        stratum/memutil.cpp
        stratum/osmemory.cpp)

target_compile_features(Stratum PUBLIC cxx_std_17)

target_include_directories(Stratum PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

set_target_properties(Stratum PROPERTIES POSITION_INDEPENDENT_CODE ON)
