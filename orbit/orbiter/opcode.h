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

    enum class CallMode : U8 {
        FASTCALL = 0,

        KW_ARG = 1,
        NARGS = 1 << 1,
        REST_ARG = 1 << 2,

        METHOD = 1 << 3
    };

    enum class ClassFlags : U8 {
        EXTEND = 0x1
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

    enum class LoadConstantMode : U8 {
        OFFSET = 0,
        FALSE,
        TRUE,
        NIL
    };

    enum class LoadFuncFlags : U16 {
        SIMPLE = 0,

        ASYNC = 0x1,
        INIT = 1 << 2,
        METHOD = 1 << 3,
        NPARAMS = 1 << 4,
        KW_PARAMS = 1 << 5,
        REST_PARAMS = 1 << 6,
        GENERATOR = 1 << 7
    };

    enum class LoadObjectPropFlags : U8 {
        INLINE = 0,
        KEY = 1,

        SUPER = 1 << 1
    };

    enum class MembershipFlags : U8 {
        IN = 0x0,
        NOT_IN = 0x1
    };

    enum class PendingAction : U8 {
        NONE,
        BREAK,
        CONTINUE,
        RETURN
    };

    enum class PushIfFlags : U8 {
        EQ = 0x0,
        NEQ = 0x1,
        TPEQ = 1 << 1,
        TNEQ = 1 << 2,

        METHOD = 1 << 3
    };

    enum class VariableFlags : U8 {
        VARIABLE = 0x0,
        CONSTANT = 0x1,
        PROTECTED = 0x1 << 1,
        PUBLIC = 0x1 << 2,
        WEAK = 0x1 << 3,
        CP_INLINE = 0x1 << 4,
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

        PANIC, // Start panic               OPCODE | 4 RESERVED | 4 SRC | 16 RESERVED
        RET, // Return instruction          OPCODE | 4 RESERVED | 4 SRC | 16 POP VALUES
        RETSUB, // Return from code object  OPCODE | 4 RESERVED | 4 SRC | 16 RESERVED
        YLD, // Yield instruction           OPCODE | 4 RESERVED | 4 SRC | 16 RESERVED

        CALL, // Call function:                     OPCODE | 4 FLAGS(CallMode) | 4 SRC | 16 ARITY
        DEFER, // Defer function:                   OPCODE | 4 FLAGS(CallMode) | 4 SRC | 16 ARITY
        EXECDEFER, // Execute deferred functions:    OPCODE | 24 RESERVED
        EXECSUB, // Execute code object directly:    OPCODE | 4 RESERVED        | 4 SRC | 16 RESERVED

        // Load/Store Operations
        LDCODE, // Load Code from Code object:      OPCODE | 4 DST | 4 RESERVED  | 16 OFFSET
        LDCST, // Load constr from Code object:     OPCODE | 4 DST | 4 FLAGS(LoadConstantMode) | 16 OFFSET
        LDIMM, // Load immediate into register:     OPCODE | 4 DST | 4 SHIFT     | 16 IMM
        MOV, // Copy value between registers:       OPCODE | 4 DST | 4 SRC       | 16 RESERVED
        MOWN, // Move value between registers:      OPCODE | 4 DST | 4 SRC       | 16 RESERVED (Move ownership)

        SETPROP, // Set type property:               OPCODE | 4 DST (TypeInfo) | 4 SRC | 4 V_SRC | 12 OFFSET

        NGBLV, // Create new context variable:                      OPCODE | 4 FLAGS    | 4 SRC   | 16 KEY OFFSET
        STGBL, // Store value into global variable using key:       OPCODE | 4 RESERVED | 4 SRC   | 16 KEY OFFSET
        STGOFF, // Store value into global variable using offset:   OPCODE | 4 RESERVED | 4 SRC   | 16 UNSIGNED OFFSET
        LDCLO, // Load closure from function object:                OPCODE | 4 BASE_REG | 4 SRC   | 16 RESERVED
        LDGBL, // Load value from global variable using key:        OPCODE | 4 DST | 4 RESERVED   | 16 KEY OFFSET
        LDGOFF, // Load value from global variable using offset:    OPCODE | 4 DST | 4 RESERVED   | 16 UNSIGNED OFFSET

        SKLDR,
        // Load from(BASE_REG + OFFSET) stack into register  OPCODE | 4 DST      | 4 BASE_REG   | 16 SIGNED OFFSET
        SKSTR,
        // Store register into (BASE_REF + OFFSET)           OPCODE | 4 BASE_REG | 4 SRC        | 16 SIGNED OFFSET

        PUSH, // Push value onto stack:             OPCODE | 4 RESERVED | 4 SRC | 20 RESERVED
        PUSHIF, // Push value onto stack IF:        OPCODE | 4 SRC | 4 TEST | 4 AGAINST | 12 FLAGS(PushIfFlags)
        POP, // Pop value from stack:               OPCODE | 4 DST | 20 RESERVED
        POPN, // Pop N values from stack:           OPCODE | 8 RESERVED | 16 UNSIGNED OFFSET

        CLONEW, // Create new closure object:       OPCODE | 4 DST | 4 RESERVED | 16 UNSIGNED SLOTS
        CLOLDR, // Load from closure object:        OPCODE | 4 DST | 4 RESERVED | 16 UNSIGNED OFFSET
        CLOSTR, // Store to closure object:         OPCODE | 4 SRC | 4 RESERVED | 16 UNSIGNED OFFSET

        // Allocate space for N variable on stack
        // Format: OPCODE | 4 RESERVED | 4 FLAGS(AllocaFlags) | 16 UNSIGNED SLOTS
        ALLOCA,

        LDFUNC, // Create function from Code object:    OPCODE | 4 DST | 4 SRC_CODE | 4 SRC_DARGS | 8 FLAGS | 4 RESERVED

        // Container object
        NDICT, // Create new dict                   OPCODE | 4 DST | 4 RESERVED | 16 UNSIGNED OFFSET
        NERROR, // Create new error                 OPCODE | 4 DST | 4 KEY | 4 DESC | 4 aux | 8 RESERVED
        NLIST, // Create new list                   OPCODE | 4 DST | 4 RESERVED | 16 UNSIGNED OFFSET
        NSET, // Create new set                     OPCODE | 4 DST | 4 RESERVED | 16 UNSIGNED OFFSET
        NTUPLE, // Create new tuple                 OPCODE | 4 DST | 4 RESERVED | 16 UNSIGNED OFFSET
        ADDELEM, // Push value to container         OPCODE | 4 DST (Container) | 4 SRC | 4 V_SRC | 12 RESERVED

        // Class/Trait
        LDINIT, // Load class initializer           OPCODE | 4 DST | 4 SRC | 16 UNSIGNED OFFSET
        LDOBJP,
        // Load object property             OPCODE | 4 DST | 4 SRC_R(object) | 4 SRC_L(LoadObjectPropFlags) | 12 UNSIGNED OFFSET
        STOBJP,
        // Store object property    OPCODE | 4 DST(Object) | 4 SRC_R(value) | 4 SRC_L(LoadObjectPropFlags) | 12 UNSIGNED OFFSET
        MKCLZ, // Create new class                  OPCODE | 4 DST | 4 Flags(ClassFlags) | 8 RESERVED | 8 IMPLs COUNT
        MKTRT, // Create new trait                  OPCODE | 4 DST | 12 RESERVED         | 8 IMPLs COUNT
        NOBJ, // Create new object                  OPCODE | 4 DST | 4 SRC(Class type)   | 16 UNSIGNED OFFSET

        // Jump Instructions
        JEN, // Jump if nil:                        OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JERR, // Jump if error match                OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JF, // Jump if false:                       OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JT, // Jump if true:                        OPCODE | 4 RESERVED | 4 SRC | 16 OFFSET
        JMP, // Unconditional jump:                 OPCODE | 24 OFFSET

        // Sync block Operations
        // Format: OPCODE | 4 RESERVED | 4 SRC | 16 RESERVED
        SYNC_ENTER,
        SYNC_EXIT,

        // Try/Catch/Finally operations
        TRY_BEGIN, // Begin try block:              OPCODE | 24 OFFSET
        TRY_END,
        TRY_SPA, // Jump if false:                  OPCODE | 2 PENDING OP | 4 SRC | 18 JMP OFFSET
        LDEXC,
        RETHROW
    };
}

ENUMBITMASK_ENABLE(orbiter::ArithFlags);

ENUMBITMASK_ENABLE(orbiter::CallMode);

ENUMBITMASK_ENABLE(orbiter::ComparisonMode);

ENUMBITMASK_ENABLE(orbiter::LoadFuncFlags);

ENUMBITMASK_ENABLE(orbiter::LoadObjectPropFlags);

ENUMBITMASK_ENABLE(orbiter::VariableFlags);

#endif // !ORBIT_ORBITER_OPCODE_H_
