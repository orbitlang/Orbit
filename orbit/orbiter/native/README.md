# Orbit FFI

This directory contains the implementation of Orbit's Foreign Function Interface (FFI), which allows the Orbit Virtual Machine to invoke native C functions dynamically.

## Directory Structure

```
native/
├── arch/                  # Architecture-specific assembly stubs
│   ├── arm64/             # ARM64 (Apple Silicon, Linux ARM64)
│   └── x86_64/            # x86_64 (macOS/Linux System V ABI)
├── dlwrap.cpp/h           # Dynamic library loading wrapper
├── ffi.h                  # Public FFI interface
├── ffi_internal.h         # Internal FFI definitions
├── loader.cpp/h           # Native library loader
├── prepare.cpp            # Parameter marshalling and type conversion
└── ucall.cpp              # Universal call dispatcher
```

## Architectural Design

The FFI engine is designed to be a pragmatic balance between portability and low-level control. Since standard C++ does not provide granular control over CPU registers and stack frames during dynamic calls, Orbit uses a hybrid approach:

1.  **C++ Marshalling Layer** (`prepare.cpp`, `ucall.cpp`): Handles the conversion of Orbit objects into native C types and prepares the necessary data buffers. It also manages a portable dispatching system for standard General Purpose Register (GPR) calls.
2.  **Native Calling Stubs (Assembly)** (`arch/*/`): To strictly adhere to various platform ABIs (Application Binary Interfaces), Orbit employs specialized assembly stubs. These stubs are responsible for "shaping" the CPU state (registers and stack) according to the target platform's requirements before jumping to the native function.

## The Role of Assembly

Assembly is essential in this engine to handle scenarios where C++ type-safety and abstraction prevent direct manipulation of the machine's state:

*   **Register Steering**: On architectures like ARM64 and x86_64 (System V), floating-point arguments must be placed in specific SIMD registers (D0-D7 or XMM0-XMM7) regardless of the presence of integer arguments. Assembly stubs ensure these registers are "pre-loaded" with the correct bit patterns.
*   **Platform Specificity**:
    *   **ARM64 (macOS/Linux)**: Uses independent register sets for integers (X0-X7) and floats (D0-D7). The `fpu_preload` stub preloads floating-point registers according to the AAPCS64 calling convention.
    *   **x86_64 (macOS/Linux System V)**: Uses independent register sets for integers (RDI, RSI, RDX, RCX, R8, R9) and floats (XMM0-XMM7). The `fpu_preload` stub handles SSE register population.
    *   **Windows x64**: Employs a unique "interleaved" convention where registers are coupled based on their argument position. A dedicated stub populates both GPR (RCX, RDX, R8, R9) and XMM (XMM0-XMM3) registers for each argument slot simultaneously.

## Current Limitations

As the FFI engine is in its early stages, the following limitations apply:

-   **Maximum Arity**: Standard calls are limited to **16** total arguments.
-   **Floating-Point Limit**: A maximum of **8** floating-point arguments (`float` or `double`) are currently supported, as they must fit within the hardware registers.
-   **Mixed-Type Stack Overflow**: Native calls involving both floating-point and standard types that exceed register capacity (forcing arguments onto the stack) are currently unsupported. Standard GPR-only calls do not share this limitation.
-   **Complex Types**: Passing or returning `structs` by value is not supported. Use pointers (`ptr`).

## Build System Integration

The FFI engine uses CMake to automatically select and compile the correct assembly stubs for the target architecture:

```cmake
# From orbit/CMakeLists.txt
enable_language(ASM)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    file(GLOB ASM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/orbiter/native/arch/arm64/*.S")
    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    file(GLOB ASM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/orbiter/native/arch/x86_64/*.S")
    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
endif()
```

The `ORBIT_HAS_FFI_STUB` preprocessor definition enables floating-point FFI support in `ucall.cpp`.

## Adding New Architectures

The approach for adding a new architecture depends on the complexity of its calling convention:

### Standard Calling Conventions (macOS/Linux System V style)

For platforms where integer and floating-point registers are independent (ARM64, x86_64 System V), you need to implement two assembly stubs:

1.  **Create the architecture directory**: Add a new subdirectory under `arch/<arch>/` (e.g., `arch/riscv64/`).

2.  **Implement `fpu_preload`**: Preloads floating-point registers before the call. This stub must load up to 8 doubles from a buffer into the FP registers defined by the ABI:
    ```c
    extern "C" void fpu_preload(double *floats);
    ```
    Example: ARM64 loads D0-D7, x86_64 loads XMM0-XMM7.

3.  **Implement `fpu_get_return`**: Retrieves the floating-point return value after the call. This stub must extract the FP return value from the appropriate register:
    ```c
    extern "C" double fpu_get_return();
    ```
    Example: ARM64 reads D0, x86_64 reads XMM0.

4.  **Handle platform-specific symbol naming**: Ensure the stubs respect local ABI conventions (e.g., underscore prefixes on macOS with the `SYMBOL()` macro).

5.  **Update CMake configuration**: Add a new branch in `orbit/CMakeLists.txt` to detect your architecture via `CMAKE_SYSTEM_PROCESSOR` and include the appropriate `.S` files with the `ORBIT_HAS_FFI_STUB` definition.

6.  **Test thoroughly**: Verify that native calls with floating-point arguments and return values work correctly.

### Complex Calling Conventions (Windows x64 style)

For platforms with interleaved or tightly coupled register conventions (like Windows x64), the simple preload/get approach is insufficient. You must implement a complete assembly-based `FFICall` dispatcher:

1.  **Create a dedicated stub directory**: Add `arch/<platform>/` (e.g., `arch/win64/`).

2.  **Implement a full `FFICall` stub**: This assembly function must handle the entire call sequence, including register population, stack alignment, and the actual call. The signature varies by platform but typically resembles:
    ```c
    extern "C" void* FFICall(void *func, const ParamInfo *args, U16 arity);
    ```
    On Windows x64, this stub must interleave GPR (RCX, RDX, R8, R9) and XMM (XMM0-XMM3) registers for each argument slot.

3.  **Conditional compilation**: Use preprocessor guards to select between the C++ generic dispatcher (`ucall.cpp`) and your platform-specific assembly stub.

4.  **Update CMake and defines**: Configure CMake to include the platform-specific stub and define appropriate macros (e.g., `ORBIT_WIN64_FFI_STUB`).

5.  **Extensive testing**: Complex stubs require rigorous testing with various argument combinations, especially mixed integer/float scenarios.