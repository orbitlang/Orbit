# Orbit FFI

This directory contains the implementation of Orbit's Foreign Function Interface (FFI), which allows the Orbit Virtual Machine to invoke native C functions dynamically.

## Directory Structure

```
native/
├── arch/                        # Architecture-specific assembly stubs
│   ├── arm64/                   # ARM64 (Apple Silicon, Linux ARM64)
│   │   └── preload.S            # fpu_preload + fpu_get_return (AAPCS64)
│   └── x86_64/                  # x86_64
│       ├── preload.S            # fpu_preload + fpu_get_return (System V AMD64, Linux/macOS)
│       └── win64/               # Windows x64 specific
│           ├── ffi_call.asm     # Full FFI call dispatcher (MS x64 ABI)
│           └── preload.asm      # fpu_get_return (MS x64 ABI)
├── dlwrap.cpp/h                 # Dynamic library loading wrapper
├── ffi.h                        # Public FFI interface
├── ffi_internal.h               # Internal FFI definitions (ParamInfo, PrepareCall)
├── loader.cpp/h                 # Native library loader
├── prepare.cpp                  # Parameter marshalling and type conversion
├── ucall.cpp                    # Universal call dispatcher (non-Windows)
└── w64call.cpp                  # Windows x64 call dispatcher
```

## Architectural Design

The FFI engine is designed to be a pragmatic balance between portability and low-level control. Since standard C++ does not provide granular control over CPU registers and stack frames during dynamic calls, Orbit uses a hybrid approach:

1. **C++ Marshalling Layer** (`prepare.cpp`, `ucall.cpp`, `w64call.cpp`): Handles the conversion of Orbit objects into native C types and prepares the `ParamInfo` array. On non-Windows platforms it also provides a portable C++ dispatcher (`FFICall`) for GPR-only calls.
2. **Native Calling Stubs (Assembly)** (`arch/*/`): To strictly adhere to various platform ABIs, Orbit employs specialized assembly stubs. These stubs are responsible for shaping the CPU state (registers and stack) according to the target platform's requirements before jumping to the native function.

## The Role of Assembly

Assembly is essential in this engine to handle scenarios where C++ type-safety and abstraction prevent direct manipulation of the machine's state:

- **FP Register Preloading**: On ARM64 and x86_64 System V, floating-point arguments must be placed in dedicated SIMD registers (D0-D7 or XMM0-XMM7) independently of integer arguments. The `fpu_preload` stub pre-loads these registers from a buffer prepared by `PrepareCall` before the C++ dispatcher issues the call.
- **FP Return Value Retrieval**: `FFICall` and `ffi_call` both return `void*` in a GPR. When the native function returns a `float` or `double`, the value sits in XMM0 (or D0 on ARM64) and is invisible to C++. `fpu_get_return` exposes it by returning as a `double` through the normal ABI, relying on the fact that XMM0/D0 is undisturbed between the native call and this stub.
- **Full Call Dispatch on Windows x64**: The MS x64 ABI uses an interleaved register convention where argument slot N maps to either RCX/RDX/R8/R9 or XMM0-XMM3 depending on the type, but never both simultaneously. This cannot be expressed in portable C++, so `ffi_call.asm` implements the entire dispatch: it walks the `ParamInfo` array, routes each argument to the correct GPR or XMM register, allocates stack space for arguments beyond the first four, aligns the stack, and performs the call.

## Platform Details

| Platform | Integer registers | FP registers | Dispatcher |
|---|---|---|---|
| ARM64 (macOS/Linux) | X0–X7 | D0–D7 | `FFICall` (C++) + `fpu_preload` |
| x86_64 Linux/macOS | RDI, RSI, RDX, RCX, R8, R9 | XMM0–XMM7 | `FFICall` (C++) + `fpu_preload` |
| Windows x64 | RCX, RDX, R8, R9 (positional) | XMM0–XMM3 (positional) | `ffi_call` (full assembly) |

## Current Limitations

- **Maximum Arity**: Standard calls are limited to **16** total arguments.
- **Floating-Point Limit**: A maximum of **8** floating-point arguments (`float` or `double`) are supported, as they must fit within the hardware FP registers.
- **Mixed-Type Stack Overflow**: Native calls involving both floating-point and standard types that exceed register capacity (forcing arguments onto the stack) are unsupported on non-Windows platforms. Standard GPR-only calls do not share this limitation. On Windows x64 this is handled correctly by `ffi_call.asm`.
- **Complex Types**: Passing or returning `structs` by value is not supported. Use pointers (`ptr`).

## Build System Integration

The FFI engine uses CMake to automatically select and compile the correct assembly stubs for the target architecture:

```cmake
# From orbit/CMakeLists.txt

# On Windows MSVC the assembler language is ASM_MASM; everywhere else plain ASM.
if(MSVC)
    enable_language(ASM_MASM)
else()
    enable_language(ASM)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    file(GLOB ASM_SRC_ARCH ".../arch/arm64/*.S")
    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(MSVC)
        # MASM (ml64) requires .asm extension
        file(GLOB ASM_SRC_ARCH ".../arch/x86_64/win64/*.asm")
    else()
        file(GLOB ASM_SRC_ARCH
            ".../arch/x86_64/*.S"
            ".../arch/x86_64/*/*.S")
    endif()
    target_compile_definitions(Orbiter PRIVATE ORBIT_HAS_FFI_STUB)
endif()
```

The `ORBIT_HAS_FFI_STUB` preprocessor definition enables floating-point FFI support `ucall.cpp`.

## Adding New Architectures

### Standard Calling Conventions (ARM64 / x86_64 System V style)

For platforms where integer and floating-point registers are independent, implement two assembly stubs in a new `arch/<arch>/preload.S`:

1. **`fpu_preload`**: Loads up to 8 doubles from a buffer into the FP argument registers defined by the ABI.
   ```c
   extern "C" void fpu_preload(double *floats);
   ```

2. **`fpu_get_return`**: A bare `ret` — the compiler already knows the function returns `double`, so XMM0/D0 is the return register and no explicit move is needed.
   ```c
   extern "C" double fpu_get_return();
   ```

3. Handle platform-specific symbol naming with the `SYMBOL()` macro (underscore prefix on macOS).

4. Add a CMake branch to detect the architecture via `CMAKE_SYSTEM_PROCESSOR`, glob the `.S` files, and define `ORBIT_HAS_FFI_STUB`.

### Complex Calling Conventions (Windows x64 style)

For platforms with interleaved or tightly coupled register conventions, a complete assembly dispatcher is required:

1. Implement `ffi_call` in a platform-specific `.asm` (or `.S`) file under `arch/<platform>/`. It must handle the full call sequence: argument routing to GPRs and FP registers, stack allocation, alignment, and the call itself.
   ```c
   extern "C" void *ffi_call(void *func, const ParamInfo *args, U16 argc, bool stack_only);
   ```

2. Implement `fpu_get_return` (can be a bare `ret` if the ABI uses XMM0 for FP returns).

3. Write a platform-specific `<platform>call.cpp` that calls `ffi_call` and handles the FP return value via `fpu_get_return`, guarded by the appropriate platform macro.

4. Update CMake to include the new files and define `ORBIT_HAS_FFI_STUB`.

5. Test thoroughly with mixed integer/float argument combinations and FP return values.
