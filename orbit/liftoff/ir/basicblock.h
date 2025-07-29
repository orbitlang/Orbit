// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BASICBLOCK_H_
#define ORBIT_LIFTOFF_IR_BASICBLOCK_H_

#include <unordered_set>

#include <orbit/liftoff/symtable.h>

#include <orbit/liftoff/ir/instruction.h>

namespace liftoff::ir {
    /**
     * @class BasicBlock
     * @brief Represents a basic block in the Intermediate Representation (IR).
     *
     * A basic block is a fundamental unit of the program's control flow graph.
     * It contains a list of instructions and tracks variable definitions
     * and usages within the block.
     */
    class BasicBlock final : public Object {
        /// Represents the set of variables defined within a basic block.
        std::unordered_set<const Symbol *> def_;

        /// Represents the set of variables used within a basic block.
        std::unordered_set<const Symbol *> use_;

        /// Represents the set of variables that are live at the entry point of the basic block.
        std::unordered_set<const Symbol *> live_in_;

        /// Represents the set of variables that are live at the exit point of the basic block.
        std::unordered_set<const Symbol *> live_out_;

        friend class IRContext;

    public:
        /// Pointer to the next basic block in the control flow.
        BasicBlock *next = nullptr;

        /// Pointer to the previous basic block in the control flow.
        BasicBlock *prev = nullptr;

        /// Pointer to an alternative `BasicBlock` within the control flow.
        BasicBlock *alt = nullptr;

        /**
         * @struct instr
         * @brief Represents a container for managing a list of instructions.
         *
         * This structure defines the head and tail pointers for a doubly linked list
         * of `Instruction` objects. It is used to maintain the sequence of instructions
         * within a basic block, facilitating traversal and modification of the instruction list.
         */
        struct {
            Instruction *head = nullptr;
            Instruction *tail = nullptr;
        } instr;

        /// Offset representing the starting position of the block (optional usage).
        U32 offset = 0;

        /// The size of the block in bytes (optional usage).
        U32 size = 0;

        /**
        * @brief Constructs a new `BasicBlock` object.
        */
        explicit BasicBlock() noexcept: Object(ObjectType::BASIC_BLOCK) {
        }

        /**
         * @brief Checks if the instruction list for the basic block is empty.
         *
         * @return `true` if the instruction list is empty, `false` otherwise.
         */
        [[nodiscard]] bool IsInstructionListEmpty() const noexcept {
            return instr.head == nullptr;
        }

        /**
         * @brief Adds a new instruction to the basic block.
         *
         * @param instr Pointer to the instruction to be added.
         */
        void AddInstruction(Instruction *instr) noexcept {
            instr->basic_block = this;
            this->size += 4;


            if (this->instr.head == nullptr) {
                this->instr.head = instr;
                this->instr.tail = instr;

                return;
            }

            instr->prev = this->instr.tail;
            this->instr.tail->next = instr;
            this->instr.tail = instr;
        }

        /**
         * @brief Adds an instruction after a specified instruction within a basic block.
         *
         * Updates the pointers of the given instructions appropriately to ensure
         * they are correctly linked in the control flow graph. Adjusts the size
         * of the basic block to account for the added instruction.
         *
         * @param instr The existing instruction after which the new instruction is to be added.
         * @param after The new instruction to be inserted after the specified instruction.
         */
        void AddInstructionAfter(Instruction *instr, Instruction *after) noexcept {
            after->next = instr->next;
            after->prev = instr;

            if (instr->next != nullptr)
                instr->next->prev = after;

            instr->next = after;

            this->size += 4;
        }

        /**
         * @brief Inserts an instruction before another instruction in the instruction list of a basic block.
         *
         * This method updates the doubly linked list of instructions within the basic block to insert
         * the specified `before` instruction immediately before the given `instr` instruction. The method
         * also updates the size of the basic block.
         *
         * @param instr Pointer to the existing `Instruction` object before which the new instruction
         * will be inserted.
         * @param before Pointer to the `Instruction` object to be inserted before `instr`.
         */
        void AddInstructionBefore(Instruction *instr, Instruction *before) noexcept {
            before->next = instr;
            before->prev = instr->prev;

            assert(instr->prev!=nullptr);

            instr->prev->next = before;

            instr->prev = before;

            this->size += 4;
        }

        /**
         * @brief Adds an instruction to the beginning of the instruction list in the basic block.
         *
         * @param instr Pointer to the `Instruction` object to be added to the start of the list.
         */
        void AddInstructionFirst(Instruction *instr) noexcept {
            instr->basic_block = this;

            instr->next = this->instr.head;
            this->instr.head->prev = instr;

            this->instr.head = instr;

            this->size += 4;
        }

        /**
         * @brief Removes an instruction from the instruction list within the basic block.
         *
         * This method detaches the specified instruction from the doubly linked list
         * of instructions in the basic block. It updates relevant pointers to maintain
         * the linked list structure and adjusts the size of the basic block accordingly.
         *
         * @param instr Pointer to the `Instruction` object to be removed.
         */
        void DeleteInstruction(Instruction *instr) noexcept {
            auto *next = instr->next;
            auto *prev = instr->prev;

            instr->next = nullptr;
            instr->prev = nullptr;

            if (next != nullptr)
                next->prev = prev;

            if (prev != nullptr)
                prev->next = next;

            if (this->instr.head == instr)
                this->instr.head = next;

            if (this->instr.tail == instr)
                this->instr.tail = prev;

            this->size -= 4;
        }

        /**
         * @brief Marks a variable as defined in the basic block.
         *
         * @param sym Pointer to the `Symbol` representing the variable.
         */
        void DefVar(const Symbol *sym) {
            this->def_.insert(sym);
        }

        /**
         * @brief Marks a variable as used in the basic block.
         *
         * @param sym Pointer to the `Symbol` representing the variable being used.
         *
         * @details
         * If the variable was defined in the same block, it is not added to the
         * `use_` set since it is already defined.
         */
        void UseVar(const Symbol *sym) {
            if (this->def_.find(sym) == this->def_.end())
                this->use_.insert(sym);
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_BASICBLOCK_H_
