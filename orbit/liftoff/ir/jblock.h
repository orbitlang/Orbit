// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_JBLOCK_H_
#define ORBIT_LIFTOFF_IR_JBLOCK_H_

#include <orbit/liftoff/ir/basicblock.h>

namespace liftoff::ir {
    class Builder;

    enum class JBlockType {
        FOR_IN,
        LABEL,
        LOOP,
        NIL_SAFE,
        TCF,
        SYNC,
        SWITCH
    };

    class JBlock {
        Builder *builder_;

    public:
        JBlock *prev = nullptr;

        JBlockType type;

        JBlock(Builder *builder, JBlockType type);

        ~JBlock();

        JBlock(const JBlock &) = delete;

        JBlock &operator=(const JBlock &) = delete;

    protected:
        [[nodiscard]] Builder *GetBuilder() const { return builder_; }
    };

    /**
     * A specialized block type that represents a targetable code block,
     * used for structures such as loops and labeled statements.
     *
     * Inherits from JBlock and extends with additional capabilities to track
     * the range of basic blocks (`begin` and `end`) and an optional label.
     */
    class JBlockTarget : public JBlock {
    public:
        BasicBlock *begin = nullptr;
        BasicBlock *end = nullptr;

        orbiter::datatype::ORString *label = nullptr;

        JBlockTarget(Builder *builder, JBlockType type, orbiter::datatype::ORString *label);
    };

    /**
     * Represents a control flow block with branching behavior, such as for
     * conditional structures or try-catch-finally blocks.
     *
     * Inherits from JBlock and introduces two additional basic blocks:
     * - `alt`: An alternate execution path for the block, such as the catch block in a try-catch-finally structure.
     * - `end`: The termination block for the control flow, marking the end of the structure.
     * - `stack_slot`: When used with TCF (Try-Catch-Finally), indicates the position in the stack where the exception handling structure resides.
     *
     * This class is useful for representing branching constructs and their respective control flow targets,
     * facilitating the generation of intermediate representation in the compiler.
     */
    class JBlockBranch : public JBlock {
    public:
        BasicBlock *alt;
        BasicBlock *end;

        U16 stack_slot = 0;

        JBlockBranch(Builder *builder, JBlockType type, BasicBlock *alt, BasicBlock *end);
    };

    /**
     * A synchronization block type used to manage control flow regions where
     * synchronization mechanisms, such as locks or barriers, are required.
     *
     * Inherits from JBlock and introduces an additional `value` field to represent
     * the associated synchronization instruction. This instruction corresponds
     * to operations such as entering and exiting a synchronization region.
     *
     * Primarily used in intermediate representation (IR) generation, this class
     * facilitates the tracking and management of synchronization constructs
     * during code compilation, ensuring proper handling of synchronization-related
     * execution states.
     */
    class JBlockSync : public JBlock {
    public:
        Instruction *value;

        JBlockSync(Builder *builder, Instruction *value);
    };

    /**
     * Represents a specialized control flow block for handling switch-case constructs.
     *
     * Inherits from JBlock and extends its functionality to include a designated
     * `end` block, which marks the termination of the switch-case structure.
     *
     * This class is primarily used in the intermediate representation (IR) generation
     * phase to model and manage the control flow of switch-case statements.
     */
    class JBlockSwitch : public JBlock {
    public:
        BasicBlock *end;

        JBlockSwitch(Builder *builder, BasicBlock *end);
    };

    /**
     * Retrieves the beginning basic block of a given JBlock, if it is a targetable block
     * such as a loop, labeled statement, or similar structure.
     *
     * The function casts the input JBlock to a JBlockTarget in order to access the
     * `begin` field, which specifies the starting basic block. If the block type is
     * not targetable, the function asserts and returns nullptr.
     *
     * @param block A pointer to the JBlock whose beginning basic block is to be retrieved.
     *              The block must be of a targetable type like LOOP, FOR_IN, or LABEL.
     * @return A pointer to the BasicBlock representing the beginning of the specified JBlock,
     *         or nullptr if the block type is invalid.
     */
    BasicBlock *GetJBlockBegin(const JBlock *block);

    /**
     * Retrieves the ending basic block of the given `JBlock`.
     * The end block is determined based on the type of the provided JBlock
     * and its specific structure.
     *
     * @param block A pointer to the JBlock from which the ending basic block
     *              should be retrieved. The block must be of a valid subtype
     *              and is expected to have an associated end attribute.
     * @return A pointer to the ending `BasicBlock` of the given `JBlock`.
     *         Returns `nullptr` if the block type is invalid or an assertion
     *         fails during execution.
     */
    BasicBlock *GetJBlockEnd(const JBlock *block);

    /**
     * Searches through a chain of JBlock objects to find a block that matches
     * a specific label or is suitable based on its type.
     *
     * This function traverses the linked list of JBlock objects starting
     * with the specified `chain` pointer. If a `label` is provided, it checks
     * if any block in the chain has the same label. If no label is provided,
     * it will return the first block that is not of type SYNC. The search
     * proceeds in reverse chronological order through the chain's `prev` pointer.
     *
     * @param chain A pointer to the head of the JBlock chain to search through.
     * @param label A pointer to an ORString representing the label to match;
     *              if `nullptr`, it searches for the first non-SYNC block instead.
     * @return A pointer to the matching JBlock, or `nullptr` if no match is found.
     */
    JBlock *FindLabeledBlock(JBlock *chain, const orbiter::datatype::ORString *label);
};

#endif // !ORBIT_LIFTOFF_IR_JBLOCK_H_
