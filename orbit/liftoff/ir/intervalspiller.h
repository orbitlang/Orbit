// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_INTERVALSPILLER_H_
#define ORBIT_LIFTOFF_IR_INTERVALSPILLER_H_

#include <set>

#include <orbit/liftoff/ir/builder.h>
#include <orbit/liftoff/ir/ircontext.h>

namespace liftoff::ir {
    class IntervalSpiller {
        std::vector<const LiveInterval *> active_stack_;
        std::set<U16> free_stack_slot_;

        Builder builder_;

        U16 stack_offset_;

        bool inherit_reg_;

        /**
         * @brief Hands out a stack slot for a new spill, reusing a freed one when possible.
         *
         * @return The index of the stack slot to use.
         *
         * Pops the lowest slot from the free pool if any earlier spill has since
         * expired; otherwise allocates a fresh slot by advancing the high-water offset.
         */
        U16 GetFreeStackSlot();

        /**
         * @brief Reclaims the stack slots of spilled intervals that are dead before a given interval.
         *
         * @param interval The interval about to be spilled; its start marks the reference position.
         */
        void ExpireOldIntervals(const LiveInterval *interval);

    public:
        /**
         * @brief Constructs a spiller bound to an IR context.
         *
         * @param ir          The IR context whose function frame backs the spilled values.
         * @param inherit_reg When true, every emitted reload inherits the spilled
         *                    instruction's assigned register; when false (default),
         *                    reloads are left unassigned.
         *
         * Anchors the slot numbering at the context's current stack high-water mark
         * (stack_slots_max), so the slots this spiller allocates never collide with
         * those already reserved by earlier compilation phases.
         */
        explicit IntervalSpiller(IRContext *ir, const bool inherit_reg = false) : builder_(ir),
                                                                            stack_offset_(ir->stack_slots_max),
                                                                            inherit_reg_(inherit_reg) {
        }

        /**
         * @brief Commits the allocated stack slots before destruction.
         *
         * Invokes Commit() so the frame reservation reflects the slots this spiller
         * consumed even on early scope exit (RAII).
         */
        ~IntervalSpiller() {
            this->Commit();
        }

        /**
         * @brief Reserves in the IR context the stack slots this spiller allocated.
         *
         * If the spiller advanced past the context's high-water mark, grows the
         * function frame (via AllocStackSlots) by the delta so the emitted spill
         * offsets are backed by real reserved space. Called automatically from the
         * destructor.
         */
        void Commit();

        /**
         * @brief Spill the specified live interval after a given instruction offset.
         * 
         * This method manages reloading and storing of values to ensure proper use and
         * storage of register-bound values when spilled to a stack or global memory slot.
         *
         * @param interval A pointer to the LiveInterval to be spilled. This represents
         *                 a register's liveness range and associated use/definition information.
         * @param after    The instruction offset after which the spill and reload operations
         *                 should take effect.
         */
        void Spill(const LiveInterval *interval, U32 after);
    };
}

#endif // !ORBIT_LIFTOFF_IR_INTERVALSPILLER_H_
