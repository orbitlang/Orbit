// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_IRCONTEXT_H_
#define ORBIT_LIFTOFF_IR_IRCONTEXT_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/datatype/code.h>
#include <orbit/orbiter/datatype/list.h>

#include <orbit/liftoff/ir/basicblock.h>
#include <orbit/liftoff/ir/jblock.h>

namespace liftoff::ir {
    enum class IRContextType {
        CLASS,
        FUNCTION,
        MODULE,
        TRAIT
    };

    class CleanupEntry {
    public:
        Instruction *start;
        Instruction *end;

        orbiter::OPCode type;

        U16 slot;

        CleanupEntry(Instruction *start, Instruction *end, const orbiter::OPCode type, const U16 slot) noexcept : start(start),
            end(end), type(type), slot(slot) {
        }
    };

    /**
     * @class ExportedName
     *
     * Represents an exported name in the intermediate representation. It contains
     * a name and associated flags describing the properties of the exported variable
     * or identifier. This class is used to provide metadata for symbols exported
     * during the intermediate representation creation process.
     *
     * The name field holds the specific identifier for the exported entity, while
     * the flags indicate attributes such as its mutability within the generated code.
     */
    class ExportedName {
    public:
        orbiter::datatype::HORString name;

        orbiter::VariableFlags flags;

        U16 slot;

        ExportedName(orbiter::datatype::ORString *name, const orbiter::VariableFlags flags,
                     const U16 slot) noexcept : name(O_INCREF(name)), flags(flags), slot(slot) {
        }
    };

    /**
     * @class LiveInterval
     *
     * Represents a live interval of a variable or temporary value in an intermediate
     * representation. It marks the range of program instructions where the value is
     * active and needs to be kept alive. These intervals are typically used during
     * register allocation to determine the lifetime of values and optimize resource usage.
     *
     * Each interval associates with a specific instruction and defines the start
     * and end points, indicating where the live range begins and ends.
     */
    class LiveInterval {
    public:
        Instruction *instr;

        U32 start;
        U32 end;

        LiveInterval(Instruction *instr, const U32 start,
                     const U32 end) noexcept : instr(instr), start(start), end(end) {
        }
    };

    /**
     * @class NativeParams
     *
     * Represents the parameters of a native function that is loaded via Foreign Function Interface (FFI).
     * This class encapsulates the name and type of the native function parameter, providing essential metadata
     * for FFI operations.
     */
    class NativeParams {
    public:
        orbiter::datatype::HORString name;
        orbiter::datatype::NativeType type;

        NativeParams(orbiter::datatype::ORString *name,
                     const orbiter::datatype::NativeType type) noexcept : name(O_INCREF(name)), type(type) {
        }
    };

    /**
     * @class NativeBinding
     *
     * Represents a configuration for runtime binding of native functions or variables.
     * This class provides the necessary details to perform Foreign Function Interface (FFI)
     * operations by defining metadata about the native entities and their expected behavior.
     *
     * The class contains information such as the name of the binding, the symbol it maps to,
     * the associated dynamic library, and the parameter types. It also includes the return
     * type of the function or variable and the binding type to describe how the binding
     * should be processed.
     */
    class NativeBinding {
    public:
        orbiter::datatype::HORString name;
        orbiter::datatype::HORString symbol;
        orbiter::datatype::HORString library;

        std::vector<NativeParams> params;

        orbiter::datatype::NativeType ret_type;

        orbiter::native::NativeBindingType binding_type;
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
        /**
         * @var active_regs_
         *
         * Maps symbols to their corresponding register instructions within the
         * intermediate representation context. This unordered map is used to track
         * the active registers associated with specific symbols, ensuring efficient
         * management and retrieval of instructions during IR generation and execution.
         */
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

        friend class Builder;

        explicit IRContext(orbiter::Isolate *isolate,
                           const IRContextType type) noexcept : isolate_(isolate), type_(type) {
        }

        ~IRContext() noexcept;

        /**
         * @brief Pushes a new sub-context onto the stack of sub-contexts maintained by this IRContext instance.
         *
         * Allocates or resizes memory for sub-context storage as needed, and establishes the linkage
         * between the current context and the newly added sub-context.
         *
         * @param context A pointer to the IRContext instance representing the sub-context to be pushed.
         *                This context will be managed by the current IRContext instance.
         *
         * @return The index of the newly added sub-context within the stack of sub-contexts.
         */
        U16 PushSubContext(IRContext *context);

        /**
         * @brief Adds an object to the object's linked list.
         *
         * This method updates an object's metadata to insert it
         * into a linked list structure. It ensures that the object's
         * current state properly links to the preceding nodes, allowing
         * for efficient list traversal and management.
         *
         * @param obj Pointer to the object being added to the list.
         */
        void Add2ObjList(Object *obj) noexcept {
            obj->memory_.prev = this->objs_;

            if (this->objs_ != nullptr)
                this->objs_->memory_.next = obj;

            this->objs_ = obj;
        }

        /**
         * @brief Removes an object from the object list.
         *
         * @param obj The object to remove.
         */
        void RemoveFromObjList(Object *obj) noexcept;

    public:
        std::vector<CleanupEntry> cleanup_entries_;

        std::vector<ExportedName> exported_names;
        std::vector<NativeBinding> native_bindings;

        /**
         * @variable live_intervals_
         *
         * Stores a collection of live intervals used for tracking the lifespan of variables
         * or registers during code generation. Each interval represents a continuous range
         * where a variable remains alive or allocated before it is no longer needed.
         * This data is integral for register allocation and optimizing the usage of
         * available registers or memory resources.
         */
        std::vector<LiveInterval> live_intervals_;

        orbiter::datatype::HList unknown_names;

        orbiter::datatype::HList static_values;

        orbiter::datatype::HORString name;

        orbiter::datatype::HORString doc;

        /// A pointer to the entry `BasicBlock` of the current intermediate representation (IR) context.
        BasicBlock *entry_ = nullptr;

        /// A pointer to the current `BasicBlock` of the current intermediate representation (IR) context.
        BasicBlock *current_ = nullptr;

        JBlock *j_chain = nullptr;

        union {
            U16 local_slots = 0;
            U16 param_count;
        };

        U16 vars_count = 0;

        U16 stack_slots = 0;

        U16 stack_slots_max = 0;

        U16 stack_push_count = 0;

        U16 stack_push_max = 0;

        U16 deferred = 0;

        U32 deferred_stack_count = 0;

        /// Represents the type of the intermediate representation (IR) context.
        IRContextType type_;

        /**
         * @brief Checks if the last active context matches the specified block type.
         *
         * This method verifies whether the current chain exists and whether
         * its type matches the specified block type.
         *
         * @param type The block type to check against the last active context.
         * @return True if the last active context exists and matches the specified type; false otherwise.
         */
        [[nodiscard]] bool CheckLastActiveContext(const JBlockType type) const {
            return this->j_chain != nullptr && this->j_chain->type == type;
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
         * @brief Searches the instruction list in reverse order to find the first instruction
         * that matches the specified opcode.
         *
         * The method begins from the tail of the instruction list and traverses it
         * backward, checking if the instruction matches the given opcode. The search
         * skips virtual instructions and only considers physical instructions.
         *
         * @param opcode The opcode of the instruction to search for.
         * @return A pointer to the first matching instruction found, or nullptr if
         *         no matching instruction exists.
         */
        [[nodiscard]] Instruction *RFindFirstInstruction(orbiter::OPCode opcode) const noexcept;

        [[nodiscard]] orbiter::Isolate *GetIsolate() const noexcept {
            return this->isolate_;
        }

        /**
         * @brief Retrieves a subcontext by its index.
         *
         * This function returns a pointer to a subcontext based on the given index `n`.
         * If the index is within the valid range, the corresponding subcontext is
         * returned. Otherwise, it returns a null pointer.
         *
         * @param n The index of the desired subcontext. Must be a non-negative integer.
         *        If `n` is out of bounds, the function will return nullptr.
         * @return A pointer to the subcontext at the specified index if it exists,
         *         or nullptr if the index is out of range.
         */
        [[nodiscard]] IRContext *GetSubContext(const int n) const noexcept {
            if (n < this->sub.count)
                return sub.context[n];

            return nullptr;
        }

        /**
         * @brief Retrieves the active context of a specific JBlockType
         *
         * Retrieves the active context of a specific type within the chain of JBlock objects,
         * if it exists. The method iterates through the linked list of JBlock objects,
         * returning the first block that matches the specified type.
         *
         * @param type The JBlockType to filter during the search for the active context.
         * @return A pointer to the JBlock object of the specified type, or nullptr if no
         *         matching block is found.
         */
        [[nodiscard]] const JBlock *GetActiveContextIf(JBlockType type) const;

        /**
         * @brief Adds a new entry into the collection of known exported names with the associated properties.
         *
         * This function inserts the given identifier and its associated flags into the exported_names
         * list maintained within the IRContext. The input specifies an identifier to be stored and its
         * configuration, encapsulated in the flags parameter. Upon adding the new entry, the function
         * returns the total number of previously stored entries before the insertion.
         *
         * @param symbol Pointer to symbol object.
         * @param flags Configuration flags associated with the identifier.
         * @return The size of the exported_names collection before the addition.
         */
        U16 ExportSymbol(const Symbol *symbol, orbiter::VariableFlags flags);

        /**
         * @brief Finds the stack slot associated with a cleanup entry matching the given start instruction.
         *
         * Searches the cleanup table for an entry whose start instruction matches the specified one,
         * and returns the stack slot where the object requiring cleanup is stored.
         * Used during code generation to resolve cleanup entries (e.g., SYNC_EXIT) to their
         * corresponding stack locations.
         *
         * @param start The instruction marking the beginning of the cleanup region.
         * @return The stack slot index of the object requiring cleanup.
         * @note Asserts if no matching entry is found.
         */
        U16 GetSlotFromCleanupMatch(const Instruction *start);

        /**
         * Retrieves the total stack count for this context.
         *
         * The stack count is computed as the sum of the allocated stack slots and the maximum
         * number of stack pushes. This provides the total stack space required for the context.
         *
         * @return The total stack count as an unsigned 16-bit integer.
         */
        [[nodiscard]] U16 GetStackCount() const noexcept {
            return this->stack_slots_max + this->stack_push_max;
        }

        /**
         * @brief Retrieves the number of subcontexts currently managed within this IRContext.
         *
         * @return The total count of subcontexts.
         */
        [[nodiscard]] U16 GetSubcontextCount() const noexcept {
            return this->sub.count;
        }

        /**
         * @brief Adds an unknown property identifier to the internal list of unknown names.
         *
         * This method appends the given property identifier to a managed list of unknown
         * property names within the IRContext instance. If the property already exists
         * in the list, its index is returned. If the identifier does not exist, it is
         * appended to the list, and the index of this newly added identifier is returned.
         *
         * The list is lazily initialized upon the first invocation of this method. If the
         * list allocation or appending operation fails, a `std::bad_alloc` exception is thrown.
         *
         * @param id Pointer to an ORString object representing the identifier of the unknown property.
         * @return The index of the property identifier in the list of unknown names as an unsigned short (U16).
         * @throws std::bad_alloc If memory allocation fails during list initialization or appending.
         */
        U16 PushUnknownProps(orbiter::datatype::ORString *id);

        /**
         * @brief Adds an unknown property identifier to the internal list of unknown names.
         *
         * This method appends the given property identifier to a managed list of unknown
         * property names within the IRContext instance. If the property already exists
         * in the list, its index is returned. If the identifier does not exist, it is
         * appended to the list, and the index of this newly added identifier is returned.
         *
         * The list is lazily initialized upon the first invocation of this method. If the
         * list allocation or appending operation fails, a `std::bad_alloc` exception is thrown.
         *
         * @param id The identifier associated with the unknown properties to be pushed.
         *           This must be a null-terminated string.
         * @return The index of the property identifier in the list of unknown names as an unsigned short (U16).
         * @throws std::bad_alloc If memory allocation fails during list initialization or appending.
         */
        U16 PushUnknownProps(const char *id);

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
         * @brief Computes the live intervals for all instructions within the intermediate representation (IR).
         *
         * This process calculates the lifetime of each instruction by iterating through the basic blocks
         * and their individual instructions, identifying the range between their starting point and their
         * last usage. The results are stored in the collection of live intervals maintained within the
         * IRContext instance.
         */
        std::vector<LiveInterval> &ComputeLiveIntervals();

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
         * @brief Deletes an IRContext instance, ensuring proper deallocation of resources.
         *
         * This method destroys the IRContext instance and deallocates its memory
         * using the associated isolate's allocator.
         *
         * @param context A pointer to the IRContext instance to be deleted. If the pointer
         *                is null, the method will return without performing any operation.
         */
        static void Delete(IRContext *context);

        void DeleteInstruction(Instruction *instruction) noexcept;

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
         * @brief Inserts an instruction immediately after a given instruction in the instruction list.
         *
         * This method adds the specified instruction `after` directly after the existing
         * instruction `instruction` in the linked list of instructions.
         *
         * @param instruction The instruction after which the new instruction will be inserted. This
         *                     must already exist in the instruction list.
         * @param after The instruction to be inserted after the specified `instruction`.
         */
        static void InsertInstructionAfter(Instruction *instruction, Instruction *after) noexcept;

        /**
         * @brief Inserts a new instruction before a specified instruction in the instruction list.
         *
         * This method modifies the instruction list by inserting a given instruction
         * immediately before another specified instruction.
         *
         * @param instruction The instruction before which the new instruction will be inserted. This
         *                     must already exist in the instruction list.
         * @param before The instruction to be inserted before the specified `instruction`.
         */
        static void InsertInstructionBefore(Instruction *instruction, Instruction *before) noexcept;

        /**
         * Sequentially assigns unique slot indexes to the instructions contained
         * across all basic blocks in the current intermediate representation context.
         *
         * This method iterates through each basic block and its corresponding
         * instructions, assigning a monotonically increasing index value to
         * each instruction. The index is set in the `instr_offset` field of the
         * instruction, which is used to uniquely identify its position within
         * the IR.
         */
        void SlotIndexes() const noexcept;
    };

    class IRCHandle {
        IRContext *ir_;

    public:
        IRCHandle() noexcept : ir_(nullptr) {
        }

        explicit IRCHandle(IRContext *ir) noexcept : ir_(ir) {
        }

        IRCHandle(const IRCHandle &) = delete;

        IRCHandle(IRCHandle &&other) noexcept : ir_(other.ir_) {
            other.ir_ = nullptr;
        }

        ~IRCHandle() noexcept {
            this->reset();
        }

        IRCHandle &operator=(const IRCHandle &) = delete;

        IRCHandle &operator=(IRCHandle &&other) noexcept {
            if (this != &other) {
                this->reset();

                this->ir_ = other.ir_;

                other.ir_ = nullptr;
            }

            return *this;
        }

        explicit operator bool() const noexcept { return this->ir_ != nullptr; }

        std::remove_pointer_t<IRContext> &operator*() const {
            assert(this->ir_ != nullptr);

            return *this->ir_;
        }

        IRContext *operator->() const noexcept { return this->ir_; }

        [[nodiscard]] IRContext *get() const noexcept { return this->ir_; }

        IRContext *release() noexcept {
            auto *temp = this->ir_;

            this->ir_ = nullptr;

            return temp;
        }

        void reset() noexcept {
            if (this->ir_ != nullptr) {
                IRContext::Delete(this->ir_);

                this->ir_ = nullptr;
            }
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRCONTEXT_H_
