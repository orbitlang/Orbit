// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SYMTABLE_H_
#define ORBIT_LIFTOFF_SYMTABLE_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/hashmap.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace liftoff {
    constexpr int kSTMapSubscopeCapacity = 6;

    using STHEntry = orbiter::datatype::HEntry<orbiter::datatype::ORString *, struct Symbol *>;
    using STHMap = orbiter::datatype::HashMap<
        orbiter::datatype::ORString *,
        Symbol *,
        orbiter::datatype::ORStringEqual,
        orbiter::datatype::ORStringHash
    >;

    enum class SymbolTableError {
        OK = 0,
        MEMORY_ERROR,
        SCOPE_NOT_FOUND,
        SYMBOL_ALREADY_EXISTS,
        SYMBOL_NOT_FOUND
    };

    enum class SymbolType {
        CLASS,
        CONSTANT,
        FUNC,
        LABEL,
        MODULE,
        NATIVE_FUNC,
        NATIVE_VAR,
        TRAIT,
        VARIABLE,
        UPVALUE,
        UNKNOWN
    };

    enum class ScopeType {
        CLASS,
        FUNCTION,
        GENERATOR,
        MODULE,
        TRAIT
    };

    enum class AccessModifier {
        PRIVATE,
        PUBLIC
    };

    class SubScope {
        STHMap symbols;

        SubScope *next = nullptr;

        SubScope *parent = nullptr;

        SubScope *sibling = nullptr;

        MSize offset_start = 0;

        MSize offset_end = 0;

        unsigned short nesting = 0;

        explicit SubScope(orbiter::Isolate *isolate) : symbols(isolate) {
        }

        friend class Scope;
        friend class SymbolTable;
    };

    class Scope {
        SubScope sub_scope;

        SubScope *active = nullptr;

        Scope *back = nullptr;

        unsigned short closure_offset = 0;

        unsigned short static_offset = 0;

        explicit Scope(orbiter::Isolate *isolate, MSize line_start) : sub_scope(isolate), line_start(line_start) {
        }

        friend class SymbolTable;

    public:
        ScopeType type = ScopeType::MODULE;

        MSize line_start = 0;

        MSize line_end = 0;

        bool closure = false;

        [[nodiscard]] bool ShouldCreateClosure() const {
            return this->closure_offset > 0;
        }

        [[nodiscard]] U16 GetClosureSize() const {
            return this->closure_offset;
        }
    };

    struct Symbol {
        Scope *defining_scope;

        Scope *scope;

        orbiter::datatype::ORString *name;

        MSize decl_offset;

        AccessModifier access;

        SymbolType type;

        unsigned short offset;

        unsigned short nesting;
    };

    class SymbolTable {
        orbiter::Isolate *isolate = nullptr;

        unsigned short *c_offset = nullptr;

        explicit SymbolTable(orbiter::Isolate *isolate): isolate(isolate) {
        }

        ~SymbolTable();

        [[nodiscard]] Scope *ScopeNew(MSize line_start) const;

        void SubScopeDel(SubScope *sub_scope, bool r_memory);

        void ScopeDel(Scope *scope);

        void SymbolDel(Symbol *symbol);

    public:
        Scope *scope = nullptr;

        SymbolTableError last_error = SymbolTableError::OK;

        /**
         * @brief Creates a new symbol table with the specified isolate.
         *
         * @param isolate Isolate associated with the symbol table.
         * @return A pointer to the newly created symbol table.
         */
        static SymbolTable *New(orbiter::Isolate *isolate) noexcept;

        /**
         * @brief Declare a new nested scope at a specified offset.
         * This method is used during parsing and creates and enters a nested scope.
         *
         * @param offset The offset used for scope declaration.
         * @return True if the nested scope was successfully declared.
         */
        bool DeclareNestedScope(MSize offset) noexcept;

        /**
         * @brief Enters a new scope with the given name.
         *
         * @param name The name of the new scope.
         * @return True if the scope was successfully entered, false otherwise.
         */
        bool EnterScope(orbiter::datatype::ORString *name) noexcept;

        /**
         * @brief Enters a new scope with the given name.
         * @param name The name of the new scope (as a C-string).
         * @return True if the scope was successfully entered, false otherwise.
         */
        bool EnterScope(const char *name) noexcept;

        /**
         * @brief Enter an existing nested scope using the specified offset.
         * This method enters a nested scope but does not create it.
         *
         * @param offset The offset used for entering the scope.
         * @return True if the scope was successfully entered, false if not found.
         */
        bool EnterNestedScope(MSize offset) const noexcept;

        /**
         * @brief Declares a new symbol with the specified name, type, and offset.
         *
         * @param name The name of the symbol.
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *Declare(orbiter::datatype::ORString *name, SymbolType type, MSize offset) noexcept;

        /**
         * @brief Declares a new symbol with the specified name, type, and offset.
         *
         * @param name The name of the symbol (as a C-string).
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *Declare(const char *name, SymbolType type, MSize offset) noexcept;

        /**
         * @brief Declares a new symbol scope with the specified details.
         *
         * @param name The name of the symbol.
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @param line_start The start line of the symbol scope.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *DeclareSymbolScope(orbiter::datatype::ORString *name, SymbolType type, MSize offset,
                                   MSize line_start) noexcept;

        /**
         * @brief Declares a new symbol scope with the specified details.
         *
         * @param name The name of the symbol (as a C-string).
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @param line_start The start line of the symbol scope.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *DeclareSymbolScope(const char *name, SymbolType type, MSize offset, MSize line_start) noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset.
         *
         * @param name The name of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found symbol or nullptr if not found.
         */
        Symbol *Lookup(orbiter::datatype::ORString *name, MSize offset) noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset.
         *
         * @param name The name of the symbol (as a C-string).
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found symbol or nullptr if not found.
         */
        Symbol *Lookup(const char *name, MSize offset) noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset, inserting it if not found.
         *
         * @param name The name of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found or newly inserted symbol.
         */
        Symbol *LookupInsert(orbiter::datatype::ORString *name, MSize offset) noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset, inserting it if not found.
         *
         * @param name The name of the symbol (as a C-string).
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found or newly inserted symbol.
         */
        Symbol *LookupInsert(const char *name, MSize offset) noexcept;

        /**
         * @brief Deletes the specified symbol table.
         *
         * @param table The symbol table to be deleted.
         */
        static void Delete(SymbolTable *table) noexcept;

        /**
         * @brief Leave the nested scope using the specified offset as end position.
         * @note This method is designed to be used during parsing.
         *
         * @param offset The offset used for scope declaration.
         */
        void LeaveNestedScope(MSize offset) const noexcept;

        /**
         * @brief Leave the nested scope.
         * @note This method is designed to step back during various compiler analyses.
         */
        void LeaveNestedScope() const noexcept {
            this->scope->active = this->scope->active->parent;
        }

        /**
         * @brief Leave the current scope, effectively ending its lifetime.
         */
        void LeaveScope(MSize offset, MSize line_end) noexcept;
    };
}

#endif // !ORBIT_LIFTOFF_SYMTABLE_H_
