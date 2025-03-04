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
        // Shift and number of bits used for representing the small integer (SMI) tag
        static constexpr unsigned char SMITagShift = 0;
        static constexpr unsigned char SMITagBits = 1;
        static constexpr uintptr_t SMITagMask = Mask(SMITag);

        // Shift and number of bits used to indicate if the object uses inline storage
        static constexpr unsigned char InlineShift = After(SMITag);
        static constexpr unsigned char InlineBits = 1;
        static constexpr uintptr_t InlineMask = Mask(Inline);

        // Shift and number of bits used for garbage collection (GC) marking
        static constexpr unsigned char GCShift = After(Inline);
        static constexpr unsigned char GCBits = 1;
        static constexpr uintptr_t GCMask = Mask(GC);

        // Shift and number of bits used for tracking strong reference counts
        static constexpr unsigned char StrongShift = After(GC);
        static constexpr unsigned char StrongBits = CounterBits(GC) - 2;
        static constexpr uintptr_t StrongMask = Mask(Strong);

        // Shift and number of bits used for tracking strong counter overflow (VFLAG)
        static constexpr unsigned char StrongVFLAGShift = After(Strong);
        static constexpr unsigned char StrongVFLAGBits = 1;
        static constexpr uintptr_t StrongVFLAGMask = Mask(StrongVFLAG);
    };

    struct GCBitOffsets {
        // Bit offset and mask for tracking if the GC has visited this object during a GC cycle
        static constexpr unsigned char VisitedShift = 0;
        static constexpr unsigned char VisitedBits = 1;
        static constexpr uintptr_t VisitedMask = Mask(Visited);

        // Bit offset and mask for determining if the object has been added to the finalization list
        static constexpr unsigned char FinalizedShift = After(Visited);
        static constexpr unsigned char FinalizedBits = 1;
        static constexpr uintptr_t FinalizedMask = Mask(Finalized);

        // Bit offset and mask to indicate if the object is a simple type (not a container)
        static constexpr unsigned char ContainerTypeShift = After(Finalized);
        static constexpr unsigned char ContainerTypeBits = 1;
        static constexpr uintptr_t ContainerTypeMask = Mask(ContainerType);

        // Bit offset and mask for the address field, representing the actual pointer value without metadata
        static constexpr unsigned char AddressShift = After(ContainerType);
        static constexpr unsigned char AddressBits = CounterBits(ContainerType);
        static constexpr uintptr_t AddressMask = Mask(Address);
    };

#undef Mask
#undef After
#undef CounterBits
}

#endif // !ORBIT_ORBITER_MEMORY_BITOFFSET_H_
