// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_VM_H_
#define ORBIT_ORBITER_VM_H_

#include <orbit/orbiter/vmstack.h>

namespace orbiter {
    constexpr auto kGeneralPurposeRegistersCount = 13;
    constexpr auto kSpecialPurposeRegistersCount = 3;
    constexpr auto kInternalRegistersCount = 2;

    using MachineWord = U32;

    union Register {
        struct {
            PtrHalfSize low; /// Low part of the register
            PtrHalfSize high; /// High part of the register
        };

        PtrSize reg; /// Full size of the register
    };

    struct Registers {
        // General Purpose Registers
        Register r0;
        Register r1;
        Register r2;
        Register r3;
        Register r4;
        Register r5;
        Register r6;
        Register r7;
        Register r8;
        Register r9;
        Register r10;
        Register r11;
        Register r12;

        // Special Purpose Registers
        Register RR; // Return Register

        Register BP; // Stack base pointer
        Register SP; // Stack pointer

        // Internal registers (not exposed and not addressable)
        Register IP; // Instruction Pointer
        Register CP; // Catch point register
    };

    enum class VMState {
        RUNNABLE,
        RUNNING,
        SUSPENDED
    };

    struct VMContext {
        Registers regs;

        VMStack stack;

        VMState state;

        bool Push(datatype::OObject *value) {
            *((datatype::OObject **) (this->stack.stack + this->regs.SP.reg)) = O_INCREF(value);
            this->regs.SP.reg += sizeof(void *);

            return true;
        }

        bool Push(PtrSize value) {
            *((PtrSize*) (this->stack.stack + this->regs.SP.reg)) = value;
            this->regs.SP.reg += sizeof(PtrSize);

            return true;
        }
    };

    datatype::OObject *eval(Fiber *fiber);
}

#endif // !ORBIT_ORBITER_VM_H_
