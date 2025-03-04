// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_BITOFFSET_H_
#define ORBIT_ORBITER_MEMORY_BITOFFSET_H_

#include <cstdint>

#include <orbit/orbiter/memory/memory.h>

namespace orbiter::memory {

#if (ORBIT_ORBITER_MEMORY_QUANTUM % 8)
#error  This header contains the fields for tagged pointer management \
        (used by ARC, GC) and needs at least 3 (less significant) bits free
#endif

#define Mask(name)          (((uintptr_t(1)<<name##Bits)-1) << name##Shift)
#define After(name)         (name##Shift + name##Bits)
#define CounterBits(name)   ((sizeof(uintptr_t) * 8) - After(name))

    struct RCBitOffsets {
        static constexpr unsigned char SMITagShift = 0;
        static constexpr unsigned char SMITagBits = 1;
        static constexpr uintptr_t SMITagMask = Mask(SMITag);

        static constexpr unsigned char InlineShift = After(SMITag);
        static constexpr unsigned char InlineBits = 1;
        static constexpr uintptr_t InlineMask = Mask(Inline);

        static constexpr unsigned char GCShift = After(Inline);
        static constexpr unsigned char GCBits = 1;
        static constexpr uintptr_t GCMask = Mask(GC);

        static constexpr unsigned char StrongShift = After(GC);
        static constexpr unsigned char StrongBits = CounterBits(GC) - 2;
        static constexpr uintptr_t StrongMask = Mask(Strong);

        static constexpr unsigned char StrongVFLAGShift = After(Strong);
        static constexpr unsigned char StrongVFLAGBits = 1;
        static constexpr uintptr_t StrongVFLAGMask = Mask(StrongVFLAG);
    };

    struct GCBitOffsets{
        static constexpr unsigned char VisitedShift = 0;
        static constexpr unsigned char VisitedBits = 1;
        static constexpr uintptr_t VisitedMask = Mask(Visited);

        static constexpr unsigned char FinalizedShift = After(Visited);
        static constexpr unsigned char FinalizedBits = 1;
        static constexpr uintptr_t FinalizedMask = Mask(Finalized);

        static constexpr unsigned char AddressShift = After(Finalized);
        static constexpr unsigned char AddressBits = CounterBits(Finalized);
        static constexpr uintptr_t AddressMask = Mask(Address);
    };

#undef Mask
#undef After
#undef CounterBits
}

#endif // !ORBIT_ORBITER_MEMORY_BITOFFSET_H_
