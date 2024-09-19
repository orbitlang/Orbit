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
#define CounterBits(name)   (sizeof(uintptr_t) * 8) - After(name)

    struct RCBitOffsets {
        static const unsigned char SMITagShift = 0;
        static const unsigned char SMITagBits = 1;
        static const uintptr_t SMITagMask = Mask(SMITag);

        static const unsigned char InlineShift = After(SMITag);
        static const unsigned char InlineBits = 1;
        static const uintptr_t InlineMask = Mask(Inline);

        static const unsigned char GCShift = After(Inline);
        static const unsigned char GCBits = 1;
        static const uintptr_t GCMask = Mask(GC);

        static const unsigned char StrongShift = After(GC);
        static const unsigned char StrongBits = CounterBits(GC) - 2;
        static const uintptr_t StrongMask = Mask(Strong);

        static const unsigned char StrongVFLAGShift = After(Strong);
        static const unsigned char StrongVFLAGBits = 1;
        static const uintptr_t StrongVFLAGMask = Mask(StrongVFLAG);
    };

#undef Mask
#undef After
#undef CounterBits
}

#endif // !ORBIT_ORBITER_MEMORY_BITOFFSET_H_
