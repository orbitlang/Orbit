// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/memory/memory.h>

#include <orbit/liftoff/symtable.h>

using namespace orbiter::datatype;
using namespace liftoff;

Scope *ScopeNew(MSize line_start) {
    auto *scope = (Scope *) orbiter::memory::Alloc(sizeof(Scope));

    if (scope != nullptr) {
        if (!scope->symbols.Initialize()) {
            orbiter::memory::Free(scope);

            return nullptr;
        }

        scope->back = nullptr;

        scope->current_nesting = 0;
        scope->closure_offset = 0;
        scope->var_offset = 0;
        scope->static_offset = 0;

        scope->line_start = line_start;
        scope->line_end = 0;
    }

    return scope;
}

STHEntry *FixMemoryOffset(Scope *scope, STHEntry *cursor, int mem_id, int nesting) {
    int memory_id = mem_id;

    while (cursor != nullptr) {
        auto *symbol = cursor->value;

        if (symbol->type == SymbolType::VARIABLE) {
            if (symbol->nesting > nesting) {
                cursor = FixMemoryOffset(scope, cursor, memory_id, symbol->nesting);

                continue;
            }

            if (symbol->nesting < nesting)
                return cursor;

            symbol->offset = memory_id++;
        }

        cursor = cursor->iter_next;
    }

    if (nesting == 0)
        scope->var_offset = memory_id;

    return nullptr;
}

void SymbolDel(Symbol *symbol) {
    if (symbol == nullptr)
        return;

    Release(symbol->name);

    if (symbol->scope != nullptr) {
        auto *scope = symbol->scope;

        for (const auto *cursor = scope->symbols.iter_begin; cursor != nullptr; cursor = cursor->iter_next)
            SymbolDel(cursor->value);

        scope->symbols.Finalize(nullptr);

        orbiter::memory::Free(scope);
    }

    orbiter::memory::Free(symbol);
}

Symbol *SymbolTable::Declare(ORString *name, SymbolType type, MSize offset) const noexcept {
    STHEntry *entry;

    if (this->scope->symbols.Lookup(name, &entry)) {
        if (entry->value->decl_offset != offset) {
            this->last_error = SymbolTableError::SYMBOL_ALREADY_EXISTS;
            return nullptr;
        }

        entry->value->type = type;

        return entry->value;
    }

    if ((entry = this->scope->symbols.AllocHEntry()) == nullptr) {
        this->last_error = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    auto *symbol = (Symbol *) orbiter::memory::Calloc(sizeof(Symbol));
    if (symbol == nullptr) {
        this->scope->symbols.FreeHEntry(entry);

        this->last_error = SymbolTableError::MEMORY_ERROR;

        return nullptr;
    }

    symbol->defining_scope = this->scope;

    symbol->name = name;

    symbol->type = type;
    symbol->decl_offset = offset;

    switch (type) {
        case SymbolType::VARIABLE:
            symbol->offset = this->scope->var_offset++;
            break;
        default:
            symbol->offset = this->scope->static_offset++;
            break;
    }

    symbol->nesting = this->scope->current_nesting;

    entry->key = name;
    entry->value = symbol;

    if (this->scope->symbols.Insert(entry)) {
        O_INCREF(name);
        O_INCREF(name);
    }

    return symbol;
}

Symbol *SymbolTable::Declare(const char *name, SymbolType type, MSize offset) const noexcept {
    auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->last_error = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->Declare(o_name.get(), type, offset);
}

Symbol *SymbolTable::DeclareSymbolScope(ORString *name, SymbolType type, MSize offset, MSize line_start) noexcept {
    STHEntry *entry;

    auto *sym = this->Declare(name, type, offset);
    if (sym == nullptr)
        return nullptr;

    auto *scope = ScopeNew(line_start);
    if (scope == nullptr) {
        if (this->scope->symbols.Remove(sym->name, &entry)) {
            Release(entry->key);

            this->scope->symbols.FreeHEntry(entry);

            SymbolDel(sym);
        }

        return nullptr;
    }

    switch (type) {
        case SymbolType::CLASS:
            scope->type = ScopeType::CLASS;
            break;
        case SymbolType::TRAIT:
            scope->type = ScopeType::TRAIT;
            break;
        case SymbolType::FUNC:
            scope->type = ScopeType::FUNCTION;
            break;
        case SymbolType::MODULE:
            assert(false);
        default:
            break;
    }

    sym->scope = scope;

    scope->back = this->scope;

    this->scope = scope;

    return sym;
}

Symbol *SymbolTable::DeclareSymbolScope(const char *name, SymbolType type, MSize offset, MSize line_start) noexcept {
    auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->last_error = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->DeclareSymbolScope(o_name.get(), type, offset, line_start);
}

Symbol *SymbolTable::Lookup(ORString *name, MSize offset) const noexcept {
    STHEntry *entry;

    const auto *cursor = this->scope;
    while (cursor != nullptr) {
        if (cursor->type != ScopeType::CLASS
            && cursor->type != ScopeType::TRAIT
            && cursor->symbols.Lookup(name, &entry)) {
            const auto *sym = entry->value;

            if (sym->defining_scope->current_nesting >= sym->nesting && sym->decl_offset <= offset)
                return entry->value;
        }

        cursor = cursor->back;
    }

    this->last_error = SymbolTableError::SYMBOL_NOT_FOUND;
    return nullptr;
}

Symbol *SymbolTable::Lookup(const char *name, MSize offset) const noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->last_error = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->Lookup(o_name.get(), offset);
}

Symbol *SymbolTable::LookupInsert(ORString *name, MSize offset) const noexcept {
    auto *sym = this->Lookup(name, offset);
    if (sym != nullptr) {
        if (sym->type != SymbolType::VARIABLE
            || sym->defining_scope == nullptr
            || this->scope == sym->defining_scope
            || sym->defining_scope->type != ScopeType::FUNCTION)
            return sym;

        sym->type = SymbolType::UPVALUE;

        sym->offset = sym->defining_scope->closure_offset++;

        sym->defining_scope->var_offset -= 1;

        return sym;
    }

    this->last_error = SymbolTableError::OK;

    return this->Declare(name, SymbolType::UNKNOWN, offset);
}

Symbol *SymbolTable::LookupInsert(const char *name, MSize offset) const noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->last_error = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->LookupInsert(o_name.get(), offset);
}

bool SymbolTable::EnterScope(ORString *name) noexcept {
    STHEntry *entry;

    if (!this->scope->symbols.Lookup(name, &entry)) {
        this->last_error = SymbolTableError::SYMBOL_NOT_FOUND;

        return false;
    }

    if (entry->value->scope == nullptr) {
        this->last_error = SymbolTableError::SCOPE_NOT_FOUND;

        return false;
    }

    entry->value->scope->back = this->scope;

    this->scope = entry->value->scope;

    return true;
}

bool SymbolTable::EnterScope(const char *name) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->last_error = SymbolTableError::MEMORY_ERROR;
        return false;
    }

    return this->EnterScope(o_name.get());
}

void SymbolTable::LeaveScope() noexcept {
    this->last_error = SymbolTableError::OK;

    auto *c_scope = this->scope;

    if (c_scope->var_offset > 0 && (c_scope->closure_offset != 0 || c_scope->current_nesting > 0))
        FixMemoryOffset(c_scope, c_scope->symbols.iter_begin, 0, 0);

    if (this->scope->type != ScopeType::MODULE)
        this->scope = c_scope->back;
}

SymbolTable *liftoff::SymbolTableNew(const orbiter::Isolate *isolate) noexcept {
    auto *table = (SymbolTable *) orbiter::memory::Alloc(sizeof(SymbolTable));
    if (table != nullptr) {
        auto *scope = ScopeNew(0);
        if (scope == nullptr) {
            orbiter::memory::Free(table);
            return nullptr;
        }

        scope->type = ScopeType::MODULE;

        table->scope = scope;

        table->isolate = isolate;

        table->last_error = SymbolTableError::OK;
    }

    return table;
}

void liftoff::SymbolTableDel(SymbolTable *table) noexcept {
    if (table == nullptr)
        return;

    auto *base = table->scope;

    while (base->type != ScopeType::MODULE)
        base = base->back;

    for (const auto *cursor = base->symbols.iter_begin; cursor != nullptr; cursor = cursor->iter_next)
        SymbolDel(cursor->value);

    base->symbols.Finalize(nullptr);

    orbiter::memory::Free(base);

    orbiter::memory::Free(table);
}
