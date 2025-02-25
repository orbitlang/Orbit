// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_VMSTACK_H_
#define ORBIT_ORBITER_VMSTACK_H_

#include <orbit/orbiter/isolate.h>

namespace orbiter {
    constexpr auto kMinStackSize = 16 * memory::kToKBytes;
    constexpr auto kMaxStackSize = 32 * memory::kToMBytes;

    constexpr auto kStackGrowthFactor = 30;
    constexpr auto kStackGrowthScalingFactor = 100;

    struct VMStack {
        Bytes stack;

        MSize capacity;
        MSize limit;

        /**
         * @brief Initializes the VMStack with the given size and stack limit.
         *
         * This method allocates memory for the VMStack and sets its capacity and limit.
         * It ensures that the stack limit is greater than the stack's size.
         * If memory allocation fails, the initialization will not succeed.
         *
         * @param isolate A pointer to the Isolate associated with this VMStack instance.
         * @param size The size of the VMStack to allocate, in bytes.
         * @param stack_limit The maximum stack size limit, which must be greater than the size.
         *
         * @return Returns true if the initialization succeeds, otherwise false.
         */
        bool Init(Isolate *isolate, MSize size, MSize stack_limit) noexcept;


        /**
         * @brief Checks if there is sufficient space in the stack to accommodate the specified size.
         *
         * If there isn't enough space, it attempts to grow the stack to fit the required size.
         * The provided size will automatically be rounded up to the nearest multiple of the slot size if necessary.
         *
         * @param isolate A pointer to the Isolate instance used for memory allocation during stack growth.
         * @param current The current usage of the stack, specified in bytes.
         * @param size The additional size in bytes required in the stack.
         *
         * @return true if there is enough space in the stack or if the stack was successfully grown;
         *         false if the stack growth failed.
         */
        bool Check(Isolate *isolate, MSize current, MSize size) noexcept;

        /**
         * @brief Grows the virtual machine stack to accommodate additional data.
         *
         * This method attempts to resize the stack if the existing capacity is insufficient
         * to allocate the requested size. The growth is determined by a balance
         * of the current capacity and predefined scaling factors. The provided size
         * will automatically be rounded up to the nearest multiple of the slot size if necessary.
         *
         * @param isolate A pointer to the Isolate instance used for memory allocation.
         * @param size    The additional size in bytes to allocate beyond the current stack size.
         *
         * @return true if the stack was successfully grown to accommodate the requested size;
         *         false if the operation failed due to exceeding the memory limits or allocation failure.
         */
        bool Grow(Isolate *isolate, MSize size) noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_VMSTACK_H_
