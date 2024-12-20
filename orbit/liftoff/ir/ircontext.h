// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_IRCONTEXT_H_
#define ORBIT_LIFTOFF_IR_IRCONTEXT_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/datatype/list.h>

#include <orbit/liftoff/ir/basicblock.h>
#include <orbit/liftoff/ir/jblock.h>

namespace liftoff::ir {
    enum class IRContextType {
        CLOSURE,
        FUNCTION,
        METHOD,
        MODULE
    };

    /**
     * @class IRContext
     *
     * Represents an intermediate representation (IR) context used to manage the state
     * during the generation of intermediate code. It handles logical counters, register
     * allocations, sub-context management, and active variable tracking. It ensures
     * allocation and deallocation of resources are managed in alignment with the associated
     * Isolate instance.
     */
    class IRContext {
        /// Maps symbols to their corresponding active instructions within the IR.
        /// The primary purpose of this mapping is to manage variable lifetimes and optimize
        std::unordered_map<const Symbol *, Instruction *> active_regs_;

        /**
         * @var objs_
         *
         * A pointer to the head of a linked list of dynamically allocated `Object` instances
         * within the current intermediate representation (IR) context. This list is utilized
         * to manage and track memory allocations associated with active objects in the IR
         * generation process. The list is updated through operations like addition and removal
         * of objects, ensuring proper memory tracking and resource management within the context.
         */
        Object *objs_ = nullptr;

        /**
         * @var isolate_
         *
         * A pointer to an instance of the `orbiter::Isolate` class that represents the execution context
         * where memory management, object lifecycle control, and isolated computations are managed.
         */
        orbiter::Isolate *isolate_ = nullptr;

        /**
         * @var back
         *
         * A pointer to the previous `IRContext` in the current context chain. This is used to
         * navigate or revert to the prior state in hierarchical or nested IR context structures.
         * It facilitates the management of parent-child relationships between contexts, ensuring
         * the ability to backtrack or transition between linked IR contexts during execution or
         * compilation processes.
         */
        IRContext *back = nullptr;

        orbiter::datatype::HList static_values;

        /**
         * @struct sub
         *
         * Represents a structure used for managing sub-contexts within an intermediate representation (IR) context.
         * This structure facilitates the organization and tracking of nested or child contexts associated with
         * a particular IRContext instance.
         *
         * The `sub` structure contains the following elements:
         * - `context`: A pointer to an array of `IRContext` pointers, representing child contexts.
         * - `count`: The current number of sub-contexts stored in the `context` array.
         * - `size`: The total capacity of the `context` array, defining how many contexts it can store before resizing.
         */
        struct {
            IRContext **context = nullptr;

            U16 count = 0;
            U16 size = 0;
        } sub;

        /**
         * @var logical_counter_
         *
         * A 32-bit unsigned integer used to maintain a logical instruction counter within the
         * intermediate representation (IR) context. This counter is incremented sequentially
         * for each instruction added, serving as a unique identifier or offset for instructions
         * during IR generation. It facilitates the tracking and ordering of instructions in the
         * current IR context.
         */
        U32 logical_counter_ = 0;

        /**
         * @var vreg_counter_
         *
         * A 32-bit unsigned integer used to maintain a virtual register counter within the
         * intermediate representation (IR) context. This counter is incremented sequentially
         * for each virtual register allocated during IR generation. It ensures unique
         * identification of virtual registers, aiding in register allocation and tracking
         * within the context.
         */
        U32 vreg_counter_ = 0;

        friend class Builder;

        explicit IRContext(orbiter::Isolate *isolate,
                           const IRContextType type) noexcept: isolate_(isolate), type_(type) {
        }

        ~IRContext() noexcept;

        U16 PushSubContext(IRContext *context);

    public:
        /// A pointer to the entry `BasicBlock` of the current intermediate representation (IR) context.
        BasicBlock *entry_ = nullptr;

        /// A pointer to the current `BasicBlock` of the current intermediate representation (IR) context.
        BasicBlock *current_ = nullptr;

        JBlock *j_chain = nullptr;

        /// Represents the type of the intermediate representation (IR) context.
        IRContextType type_;

        /**
         * @brief Increments the virtual register counter and returns its pre-incremented value.
         *
         * This method is used for obtaining a unique identifier for a virtual register
         * in the intermediate representation (IR) context by incrementing the internal
         * virtual register counter.
         *
         * @return The value of the virtual register counter before it was incremented, as an `I32`.
         */
        I32 GetIncRVirtCounter() noexcept {
            return (I32) this->vreg_counter_++;
        }

        /**
         * @brief Retrieves the last active load instruction associated with the provided symbol.
         *
         * This method checks the active register mappings to determine if a load instruction
         * for the specified symbol has been recorded. If such an instruction exists, it returns
         * the corresponding load instruction. If no entry is found, it returns a null pointer.
         *
         * @param symbol The symbol for which to find the last active variable load instruction.
         * @return A pointer to the last active load instruction associated with the symbol,
         *         or nullptr if no such instruction is found.
         */
        Instruction *GetLastActiveVariableLoad(const Symbol *symbol);

        /**
         * @brief Retrieves the number of subcontexts currently managed within this IRContext.
         *
         * @return The total count of subcontexts.
         */
        [[nodiscard]] U16 GetSubcontextCount() const noexcept {
            return this->sub.count;
        }

        /**
         * @brief Adds a static value to the internal list of static values managed by the IRContext
         * instance.
         *
         * If the list does not already exist, it is initialized. The method handles
         * resource allocation and ensures that the value is appended to the list properly.
         *
         * @param value A pointer to the OObject instance representing the static value to be added.
         * @return The index of the newly added value in the static values list as an unsigned short (U16).
         */
        U16 PushStaticValue(orbiter::datatype::OObject *value);

        /**
         * @brief Adds an active variable to the IR context.
         *
         * Updates the internal state to track the provided symbol as active, associating it
         * with the corresponding instruction.
         *
         * @param symbol A pointer to the symbol to be marked as active.
         * @param instr A pointer to the instruction associated with the active symbol.
         */
        void AddActiveVar(const Symbol *symbol, Instruction *instr);

        /**
         * @brief Invalidates a specific active variable or clears all active variables in the
         * current IR context.
         *
         * If a specific symbol is provided, it removes that symbol from the active registers.
         * If no symbol is provided, it clears all active registers.
         *
         * @param symbol A pointer to the symbol to be invalidated. If nullptr, all active variables are invalidated.
         */
        void InvalidateActiveVar(const Symbol *symbol);

        /**
         * @brief Deletes an IRContext instance, ensuring proper deallocation of resources.
         *
         * This method destroys the IRContext instance and deallocates its memory
         * using the associated isolate's allocator.
         *
         * @param context A pointer to the IRContext instance to be deleted. If the pointer
         *                is null, the method will return without performing any operation.
         */
        static void Delete(IRContext *context);
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRCONTEXT_H_
