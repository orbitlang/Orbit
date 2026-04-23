// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_BITOFFSET_H_
#define ORBIT_ORBITER_MEMORY_BITOFFSET_H_

#include <cstdint>

namespace orbiter::memory {
#if (ORBIT_ORBITER_MEMORY_QUANTUM % 8)
#error  This header contains the fields for tagged pointer management \
        (used by GC) and needs at least 1 (less significant) bits free
#endif

#define Mask(name)          (((uintptr_t(1)<<name##Bits)-1) << name##Shift)
#define After(name)         (name##Shift + name##Bits)
#define CounterBits(name)   ((sizeof(uintptr_t) * 8) - After(name))

    struct PtrBitOffsets {
        // Bit 0 — SMI tag.
        // When set, the entire pointer-sized value is a tagged integer (SMI) rather
        // than a heap pointer. Heap pointers always have this bit clear due to
        // allocation alignment guarantees (ORBIT_ORBITER_MEMORY_QUANTUM >= 8).
        static constexpr unsigned char SMITagShift = 0;
        static constexpr unsigned char SMITagBits = 1;
        static constexpr uintptr_t SMITagMask = Mask(SMITag);

        // Bit 1 — oddball discriminator.
        // SMI tag (bit 0) set with this bit clear → tagged small integer (SMI).
        // SMI tag (bit 0) set with this bit set  → oddball (true, false, nil).
        // Heap pointers have bit 0 clear (guaranteed by alignment), so this
        // bit is irrelevant for them. Keeping the discriminator in the low
        // bits (instead of the MSB) lets negative SMIs keep their sign bit
        // in the MSB without colliding with the oddball pattern.
        // See kOddBallMask in obase.h.
        static constexpr unsigned char OddBallShift = After(SMITag);
        static constexpr unsigned char OddBallBits = 1;
        static constexpr uintptr_t OddBallMask = Mask(OddBall);
    };

    struct GCBitOffsets {
        // Bit 0 — finalize flag.
        // Set when the object has a destructor that must be called before the
        // memory is reclaimed. The GC enqueues finalization for marked objects
        // before the sweep phase.
        static constexpr unsigned char FinalizedShift = 0;
        static constexpr unsigned char FinalizedBits = 1;
        static constexpr uintptr_t FinalizedMask = Mask(Finalized);

        // Bit 1 — container flag.
        // Set when the object holds references to other GC-managed objects
        // (e.g. lists, dicts, tuples). The GC uses this to decide whether to
        // recurse into the object during the mark phase.
        static constexpr unsigned char ContainerTypeShift = After(Finalized);
        static constexpr unsigned char ContainerTypeBits = 1;
        static constexpr uintptr_t ContainerTypeMask = Mask(ContainerType);

        // Bits 2..N — next pointer.
        // The actual address of the next GCHead in the intrusive linked list,
        // with the lower metadata bits masked out.
        static constexpr unsigned char AddressShift = After(ContainerType);
        static constexpr unsigned char AddressBits = CounterBits(ContainerType);
        static constexpr uintptr_t AddressMask = Mask(Address);
    };

#undef Mask
#undef After
#undef CounterBits
}

#endif // !ORBIT_ORBITER_MEMORY_BITOFFSET_H_
