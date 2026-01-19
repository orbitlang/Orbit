// Orbit Programming Language - FFI Support
//
// This function is part of Orbit's Foreign Function Interface (FFI) implementation.
// It preloads ARM64 floating-point registers (d0-d7) with double values before
// calling native C/C++ functions that expect floating-point arguments.
//
// According to the ARM64 calling convention (AAPCS64):
// - Floating-point arguments are passed in registers d0-d7 (or s0-s7 for floats)
// - This function ensures proper register setup for FFI calls involving doubles/floats

#if defined(__APPLE__)
#define SYMBOL(name) _##name
#else
#define SYMBOL(name) name
#endif

.global SYMBOL(fpu_preload)
.global SYMBOL(fpu_get_return)

.p2align 2

SYMBOL(fpu_preload):
    ldp d0, d1, [x0, #0]
    ldp d2, d3, [x0, #16]
    ldp d4, d5, [x0, #32]
    ldp d6, d7, [x0, #48]
    ret

SYMBOL(fpu_get_return):
    ret

#if defined(__APPLE__)
    .subsections_via_symbols
#endif
