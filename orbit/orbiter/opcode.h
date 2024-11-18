// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_OPCODE_H_
#define ORBIT_ORBITER_OPCODE_H_

namespace orbiter {
    // Single instruction format:
    // XXXXXXXX - XXXXXXXX - XXXXXXXX - XXXXXXXX <-- 32Bit
    //  OPCODE  - DEST|SRC -        FLAGS
    //  OPCODE  - DEST|           IMMEDIATE

    enum class OPCode {
        ADD = 1,    // ADD two values:                  OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 12 RESERVED
        DIV,        // Divide two values:               OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 12 RESERVED
        LDCST,      // Load constr from Code object:    OPCODE | 4 DST | 4 RESERVED  | 16 IMM
        LDIMM,      // Load immediate into register:    OPCODE | 4 DST | 4 SHIFT     | 16 IMM
        MOV,        // Copy value between registers:    OPCODE | 4 DST | 4 SRC       | 16 RESERVED
        MOWN,       // Move value between registers:    OPCODE | 4 DST | 4 SRC       | 16 RESERVED (Move ownership)
        MUL,        // Multiply two values:             OPCODE | 4 DST | 4 SRC_L | 4 SRC_R | 12 RESERVED
        SKLDR       // Load from stack into register:   OPCODE | 4 DST | 4 RESERVED  | 16 OFFSET
    };
}

#endif // !ORBIT_ORBITER_OPCODE_H_
