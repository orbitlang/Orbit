// Orbit Programming Language - FFI Support
//
// This file is part of Orbit's Foreign Function Interface (FFI) implementation.
// It provides two stubs for handling floating-point arguments and return values
// when calling native C/C++ functions through the FFI engine on x86_64 Linux/macOS.
//
// According to the System V AMD64 ABI:
// - Floating-point arguments are passed in XMM0..XMM7 (up to 8 doubles/floats)
// - Floating-point return values are placed in XMM0
// - fpu_preload loads the fp_args array (prepared by PrepareCall) into XMM0..XMM7
//   before FFICall dispatches the native call, so the compiler-generated call site
//   sees the correct XMM registers already set
// - fpu_get_return retrieves XMM0 after FFICall returns (which returns void*
//   in RAX), making the FP return value visible to C++ through the normal ABI

#if defined(__APPLE__)
#define SYMBOL(name) _##name
#else
#define SYMBOL(name) name
#endif

.global SYMBOL(fpu_preload)
.global SYMBOL(fpu_get_return)

.p2align 2

// Function: fpu_preload
// Signature: void fpu_preload(double *floats)
//   RDI = pointer to an array of up to 8 doubles
// Loads the array entries into XMM0..XMM7 in order, matching the SysV AMD64
// argument passing registers for floating-point parameters.
SYMBOL(fpu_preload):
    movsd xmm0, [rdi]
    movsd xmm1, [rdi + 8]
    movsd xmm2, [rdi + 16]
    movsd xmm3, [rdi + 24]
    movsd xmm4, [rdi + 32]
    movsd xmm5, [rdi + 40]
    movsd xmm6, [rdi + 48]
    movsd xmm7, [rdi + 56]
    ret

// Function: fpu_get_return
// Signature: double fpu_get_return()
// Returns: XMM0 - the floating-point return value of the previously called function
//
// FFICall returns void* in RAX, leaving any FP return value sitting in XMM0.
// This function must be called immediately after FFICall, before anything else
// can clobber XMM0, so the compiler sees the double through the normal ABI
// return register with no extra moves required.
SYMBOL(fpu_get_return):
    ret

#if defined(__APPLE__)
    .subsections_via_symbols
#endif
