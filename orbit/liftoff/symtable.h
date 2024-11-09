// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SYMTABLE_H_
#define ORBIT_LIFTOFF_SYMTABLE_H_

#include <orbit/util/hashmap.h>

#include<orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/memory/memory.h>

namespace liftoff {
    using STHEntry = HEntry<orbiter::datatype::ORString *, struct Symbol *>;
    using STHMap = HashMap<
        orbiter::datatype::ORString *,
        Symbol *,
        orbiter::memory::Alloc,
        orbiter::memory::Realloc,
        orbiter::memory::Free,
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

    struct Scope {
        STHMap symbols;

        Scope *back;

        MSize line_start;

        MSize line_end;

        unsigned short current_nesting;

        unsigned short closure_offset;

        unsigned short var_offset;

        unsigned short static_offset;

        ScopeType type;
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

    struct SymbolTable {
        const orbiter::Isolate *isolate;

        Scope *scope;

        mutable SymbolTableError last_error;

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
         * @brief Declares a new symbol with the specified name, type, and offset.
         *
         * @param name The name of the symbol.
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *Declare(orbiter::datatype::ORString *name, SymbolType type, MSize offset) const noexcept;

        /**
         * @brief Declares a new symbol with the specified name, type, and offset.
         *
         * @param name The name of the symbol (as a C-string).
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *Declare(const char *name, SymbolType type, MSize offset) const noexcept;

        /**
         * @brief Declares a new symbol scope with the specified details.
         *
         * @param name The name of the symbol.
         * @param type The type of the symbol.
         * @param offset The offset of the symbol in source code.
         * @param line_start The start line of the symbol scope.
         * @return A pointer to the newly declared symbol.
         */
        Symbol *DeclareSymbolScope(orbiter::datatype::ORString *name, SymbolType type, MSize offset, MSize line_start) noexcept;

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
        Symbol *Lookup(orbiter::datatype::ORString *name, MSize offset) const noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset.
         *
         * @param name The name of the symbol (as a C-string).
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found symbol or nullptr if not found.
         */
        Symbol *Lookup(const char *name, MSize offset) const noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset, inserting it if not found.
         *
         * @param name The name of the symbol.
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found or newly inserted symbol.
         */
        Symbol *LookupInsert(orbiter::datatype::ORString *name, MSize offset) const noexcept;

        /**
         * @brief Looks up a symbol with the specified name and offset, inserting it if not found.
         *
         * @param name The name of the symbol (as a C-string).
         * @param offset The offset of the symbol in source code.
         * @return A pointer to the found or newly inserted symbol.
         */
        Symbol *LookupInsert(const char *name, MSize offset) const noexcept;

        /**
         * Enters a nested scope, incrementing the current nesting level.
         */
        void EnterNestedScope() const noexcept {
            this->scope->current_nesting += 1;
        }

        /**
         * Leaves a nested scope, decrementing the current nesting level.
         */
        void LeaveNestedScope() const noexcept {
            this->scope->current_nesting -= 1;
        }

        /**
         * @brief Leave the current scope, effectively ending its lifetime.
         */
        void LeaveScope() noexcept;
    };

    /**
     * @brief Creates a new symbol table with the specified isolate.
     *
     * @param isolate Isolate associated with the symbol table.
     * @return A pointer to the newly created symbol table.
     */
    SymbolTable *SymbolTableNew(const orbiter::Isolate *isolate) noexcept;

    /**
     * @brief Deletes the specified symbol table.
     *
     * @param table The symbol table to be deleted.
     */
    void SymbolTableDel(SymbolTable *table) noexcept;
}

#endif // !ORBIT_LIFTOFF_SYMTABLE_H_
