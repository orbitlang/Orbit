// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_OPCODE_H_
#define ORBIT_ORBITER_OPCODE_H_

#include <orbit/datatype.h>

#include <orbit/util/enum_bitmask.h>

namespace orbiter {
    // Flags for arithmetic operations
    enum class ArithFlags : U8 {
        NONE = 0x0,     // Default integer operation
        DIV_REM = 0x1,  // Store remainder instead of quotient (MOD operation)
        FLOAT = 0x2,    // Floating point operation

        // Combinations:
        // FLOAT | DIV_REM = MODR (floating point modulo)
        // FLOAT alone = DIVR (floating point division)
        // DIV_REM alone = MOD (integer modulo)
        // NONE = DIV (integer division)
    };

    enum class EqualityMode : U8 {
        NORMAL,
        STRICT
    };

    enum class ComparisonMode : U8 {
        EQ = 0x1,    // Equal (LE, GE)
        LT = 0x2,    // Less Than (<)
        GT = 0x4,    // Greater Than (>)
    };

    enum class MembershipFlags : U8 {
        IN = 0x0,
        NOT_IN = 0x1
    };

    // Single instruction format:
    // XXXXXXXX - XXXXXXXX - XXXXXXXX - XXXXXXXX <-- 32Bit
    enum class OPCode {
        // Arithmetic Operations
        // Format: OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 4 FLAGS | 8 RESERVED
        ADD = 1,    // ADD two values
        SUB,        // Subtract two values
        MUL,        // Multiply two values
        DIV,        // Divide two values (integer)

        // Bitwise Operations
        // Format: OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 12 RESERVED
        AND,        // Bitwise AND (&)
        OR,         // Bitwise OR (|)
        XOR,        // Bitwise XOR (^)

        // Shift Operations
        // Register format:  OPCODE | 4 DST | 4 SRC | 4 SHIFT_REG | 12 RESERVED
        // Immediate format: OPCODE | 4 DST | 4 SRC | 16 IMM
        SHLR,       // Shift Left Register (value << reg)
        SHLI,       // Shift Left Immediate (value << imm)
        SHRR,       // Shift Right Register (value >> reg)
        SHRI,       // Shift Right Immediate (value >> imm)

        // Comparison & Membership Operations
        // Format: OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 12 FLAGS
        MEMB,       // Membership test (in/not in based on flag)
        CMP,        // Generic compare (flags for <,>,<=,>=)
        EQ,         // Equality (== or ===)

        NOT,

        // Load/Store Operations
        LDCST,      // Load constr from Code object:    OPCODE | 4 DST | 4 RESERVED  | 16 OFFSET
        LDIMM,      // Load immediate into register:    OPCODE | 4 DST | 4 SHIFT     | 16 IMM
        MOV,        // Copy value between registers:    OPCODE | 4 DST | 4 SRC       | 16 RESERVED
        MOWN,       // Move value between registers:    OPCODE | 4 DST | 4 SRC       | 16 RESERVED (Move ownership)
        SKLDR       // Load from stack into register:   OPCODE | 4 DST | 4 RESERVED  | 16 OFFSET
    };
}

ENUMBITMASK_ENABLE(orbiter::ArithFlags);
ENUMBITMASK_ENABLE(orbiter::ComparisonMode);

#endif // !ORBIT_ORBITER_OPCODE_H_