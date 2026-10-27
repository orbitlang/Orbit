# orbit-astdump: print the AST and the scope tree of an Orbit source file.

add_executable(orbit-astdump astdump.cpp)

# runtime.h (Orbiter::Initialize) includes gyro's header, and Orbiter links gyro
# privately, so the tool names it to get its include path.
target_link_libraries(orbit-astdump PRIVATE Orbit::Orbiter gyro::gyro)
