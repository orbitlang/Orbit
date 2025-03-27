// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_OPCODE_H_
#define ORBIT_ORBITER_OPCODE_H_

#include <orbit/datatype.h>

#include <orbit/util/enum_bitmask.h>

namespace orbiter {
    enum class AllocaFlags : U8 {
        DEFAULT,
        ZERO_INIT
    };

    // Flags for arithmetic operations
    enum class ArithFlags : U8 {
        NONE = 0x0, // Default integer operation
        DIV_REM = 0x1, // Store remainder instead of quotient (MOD operation)
        FLOAT = 0x2, // Floating point operation

        // Combinations:
        // FLOAT | DIV_REM = MODR (floating point modulo)
        // FLOAT alone = DIVR (floating point division)
        // DIV_REM alone = MOD (integer modulo)
        // NONE = DIV (integer division)
    };

    enum class ClosureLSMode : U8 {
        FUNC_SLOT = 0,
        STACK
    };

    enum class ComparisonMode : U8 {
        EQ = 0x1, // Equal (LE, GE)
        LT = 0x2, // Less Than (<)
        GT = 0x4, // Greater Than (>)
    };

    enum class EqualityMode : U8 {
        NORMAL,
        STRICT
    };

    enum class LoadFuncFlags : U8 {
        SIMPLE = 0,

        ASYNC = 0x1,
        CLOSURE = 0x2,
        DEF_ARGS = 0x4,
        GENERATOR = 0x8
    };

    enum class LoadConstantMode : U8 {
        OFFSET = 0,
        FALSE,
        TRUE,
        NIL
    };

    enum class MembershipFlags : U8 {
        IN = 0x0,
        NOT_IN = 0x1
    };

    enum class VariableFlags : U8 {
        VARIABLE = 0x0,
        CONSTANT = 0x1,
        PUBLIC = 0x2,
        WEAK = 0x4
    };

    // Single instruction format:
    // XXXXXXXX - XXXXXXXX - XXXXXXXX - XXXXXXXX <-- 32Bit
    enum class OPCode {
        // Arithmetic Operations
        // Format: OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 4 FLAGS | 8 RESERVED
        ADD = 1, // ADD two values
        SUB, // Subtract two values
        MUL, // Multiply two values
        DIV, // Divide two values (integer)

        // Bitwise Operations
        // Format: OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 12 RESERVED
        AND, // Bitwise AND (&)
        OR, // Bitwise OR (|)
        XOR, // Bitwise XOR (^)

        // Shift Operations
        // Register format:  OPCODE | 4 DST | 4 SRC | 4 SHIFT_REG | 12 RESERVED
        // Immediate format: OPCODE | 4 DST | 4 SRC | 16 IMM
        SHLR, // Shift Left Register (value << reg)
        SHLI, // Shift Left Immediate (value << imm)
        SHRR, // Shift Right Register (value >> reg)
        SHRI, // Shift Right Immediate (value >> imm)

        // Comparison & Membership Operations
        // Format: OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 4 FLAGS | 8 RESERVED
        MEMB, // Membership test (in/not in based on flag)
        CMP, // Generic compare (flags for <,>,<=,>=)
        EQ, // Equality (== or ===)

        // UNARY OP
        // Format: OPCODE | 4 DST | 4 SRC | 16 RESERVED
        MVN, // Move Not (bitwise complement)
        NEG, // Arithmetic negation (-value)
        NOT, // Logical NOT (!value)

        // Format: OPCODE | 4 RESERVED | 4 SRC | 20 RESERVED
        PANIC, // Start panic
        RET, // Return instruction
        YLD, // Yield instruction

        CALL, // Call function: OPCODE | 4 RESERVED | 4 SRC | 16 ARITY

        // Load/Store Operations
        LDCODE, // Load Code from Code object:      OPCODE | 4 DST | 4 RESERVED  | 16 OFFSET
        LDCST, // Load constr from Code object:     OPCODE | 4 DST | 4 FLAGS(LoadConstantMode) | 16 OFFSET
        LDIMM, // Load immediate into register:     OPCODE | 4 DST | 4 SHIFT     | 16 IMM
        MOV, // Copy value between registers:       OPCODE | 4 DST | 4 SRC       | 16 RESERVED
        MOWN, // Move value between registers:      OPCODE | 4 DST | 4 SRC       | 16 RESERVED (Move ownership)

        NGBLV, // Create new module variable:                       OPCODE | 4 FLAGS    | 4 SRC   | 16 UNSIGNED OFFSET
        STGBL, // Store value into global variable using key:       OPCODE | 4 RESERVED | 4 SRC   | 16 UNSIGNED OFFSET
        STGOFF, // Store value into global variable using offset:   OPCODE | 4 RESERVED | 4 SRC   | 16 UNSIGNED OFFSET
        LDGBL, // Load value from global variable using key:        OPCODE | 4 DST | 4 RESERVED   | 16 UNSIGNED OFFSET
        LDGOFF, // Load value from global variable using offset:    OPCODE | 4 DST | 4 RESERVED   | 16 UNSIGNED OFFSET
        SKLDR, // Load from stack into register:                    OPCODE | 4 DST | 4 RESERVED   | 16 SIGNED OFFSET
        SKSTR, // Store register into EBP+OFFSET                    OPCODE | 4 RESERVED | 4 SRC   | 16 SIGNED OFFSET

        PUSH, // Push value onto stack:             OPCODE | 4 RESERVED | 4 SRC | 20 RESERVED
        POP, // Pop value from stack:               OPCODE | 4 DST | 20 RESERVED

        CLONEW, // Create new closure object:       OPCODE | 8 RESERVED | 16 UNSIGNED SLOTS
        CLOLDR, // Load from closure object:        OPCODE | 4 DST      | 4 FLAGS(ClosureLSMode) | 16 UNSIGNED OFFSET
        CLOSTR, // Store to closure object:         OPCODE | 4 FLAGS(ClosureLSMode) | 4 SRC      | 16 UNSIGNED OFFSET

        // Allocate space for N variable on stack
        // Format: OPCODE | 4 RESERVED | 4 FLAGS(AllocaFlags) | 16 UNSIGNED SLOTS
        ALLOCA,

        LDFUNC, // Create function from Code object:        OPCODE | 4 DST | 4 SRC | 4 FLAGS | 12 RESERVED

        // Jump Instructions
        JEN, // Jump if nil:                        OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JF, // Jump if false:                       OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JT, // Jump if true:                        OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JMP, // Unconditional jump:                 OPCODE | 24 OFFSET

        // Sync block Operations
        // Format: OPCODE | 4 RESERVED | 4 SRC | 16 RESERVED
        SYNC_ENTER,
        SYNC_EXIT,
    };
}

ENUMBITMASK_ENABLE(orbiter::ArithFlags);

ENUMBITMASK_ENABLE(orbiter::ComparisonMode);

ENUMBITMASK_ENABLE(orbiter::LoadFuncFlags);

ENUMBITMASK_ENABLE(orbiter::VariableFlags);

#endif // !ORBIT_ORBITER_OPCODE_H_
