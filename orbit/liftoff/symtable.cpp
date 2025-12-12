// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/liftoff/symtable.h>

using namespace orbiter::datatype;
using namespace liftoff;

bool SymbolTable::DeclareNestedScope(const MSize offset) noexcept {
    orbiter::memory::IsolateAllocator allocator(this->isolate);

    auto *active = this->scope->active;

    auto *sub = allocator.alloc<SubScope>(sizeof(SubScope));
    if (sub != nullptr) {
        new(sub)SubScope(this->isolate);

        if (!sub->symbols.Initialize(kSTMapSubscopeCapacity)) {
            this->status = SymbolTableError::MEMORY_ERROR;

            allocator.free(sub);

            return false;
        }

        sub->parent = active;
        sub->offset_start = offset;
        sub->nesting = active->nesting + 1;

        if (active->sibling != nullptr) {
            for (auto *cursor = active->sibling; cursor != nullptr; cursor = cursor->next) {
                if (cursor->next == nullptr) {
                    cursor->next = sub;
                    break;
                }
            }
        } else
            active->sibling = sub;
    }

    this->scope->active = sub;

    return true;
}

bool SymbolTable::EnterScope(ORString *name) noexcept {
    STHEntry *entry;

    if (!this->scope->active->symbols.Lookup(name, &entry)) {
        this->status = SymbolTableError::SYMBOL_NOT_FOUND;

        return false;
    }

    if (entry->value->scope == nullptr) {
        this->status = SymbolTableError::SCOPE_NOT_FOUND;

        return false;
    }

    entry->value->scope->back = this->scope;

    this->scope = entry->value->scope;

    return true;
}

bool SymbolTable::EnterScope(const char *name) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return false;
    }

    return this->EnterScope(o_name.get());
}

bool SymbolTable::EnterNestedScope(const MSize offset) const noexcept {
    const auto active = this->scope->active;
    SubScope *target = nullptr;

    if (offset >= active->offset_start && offset < active->offset_end) {
        bool changed = true;

        target = active;

        while (changed && target != nullptr && target->sibling != nullptr) {
            changed = false;

            for (auto *cursor = target->sibling; cursor != nullptr; cursor = cursor->next) {
                if (offset >= cursor->offset_start && offset < cursor->offset_end) {
                    target = cursor;
                    changed = true;
                }
            }
        }
    }

    if (target != nullptr) {
        this->scope->active = target;
        return true;
    }

    return false;
}

const char *SymbolTable::GetStatusMessage() const {
    static const char *messages[] = {
        "no error",
        "memory allocation failed",
        "scope not found",
        "symbol already exists",
        "symbol not found"
    };

    return messages[(int) this->status];
}

Scope *SymbolTable::ScopeNew(const MSize line_start) const noexcept {
    orbiter::memory::IsolateAllocator allocator(this->isolate);

    auto *scope = allocator.alloc<Scope>(sizeof(Scope));
    if (scope != nullptr) {
        new(scope)Scope(isolate, line_start);

        if (!scope->sub_scope.symbols.Initialize()) {
            scope->~Scope();

            allocator.free(scope);

            return nullptr;
        }

        scope->active = &scope->sub_scope;
    }

    return scope;
}

Symbol *SymbolTable::SymbolNew(ORString *name, const SymbolType type, const MSize offset) noexcept {
    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *symbol = allocator.calloc<Symbol>(sizeof(Symbol));
    if (symbol == nullptr) {
        this->status = SymbolTableError::MEMORY_ERROR;

        return nullptr;
    }

    symbol->defining_scope = this->scope;

    symbol->name = O_FAST_INCREF(name);

    symbol->type = type;
    symbol->decl_offset = offset;

    symbol->offset = 0;
    symbol->stack_offset = 0;

    symbol->tdz = false;

    if (type != SymbolType::UNKNOWN && type != SymbolType::ECONST) {
        if (type == SymbolType::PARAMETER)
            symbol->offset = this->scope->parameter_count++;
        else if (type == SymbolType::CONSTANT || type == SymbolType::CLASS || type == SymbolType::TRAIT)
            symbol->offset = this->scope->static_offset++;
    }

    symbol->nesting = this->scope->active->nesting;

    return symbol;
}


Symbol *SymbolTable::Declare(ORString *name, const SymbolType type, const MSize offset) noexcept {
    auto *table = this->scope->active;

    STHEntry *entry;

    if (table->symbols.Lookup(name, &entry)) {
        auto *value = entry->value;

        if (value->decl_offset != offset) {
            if (value->type == SymbolType::UNKNOWN) {
                value->next = this->SymbolNew(name, type, offset);
                if (value->next == nullptr) {
                    this->status = SymbolTableError::MEMORY_ERROR;
                    return nullptr;
                }

                return value->next;
            }

            this->status = SymbolTableError::SYMBOL_ALREADY_EXISTS;
            return nullptr;
        }

        value->type = type;

        return value;
    }

    if ((entry = table->symbols.AllocHEntry()) == nullptr) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    entry->key = O_FAST_INCREF(name);
    entry->value = this->SymbolNew(name, type, offset);
    if (entry->value == nullptr) {
        O_FAST_DECREF(entry->key);

        return nullptr;
    }

    if (!table->symbols.Insert(entry)) {
        O_FAST_DECREF(entry->key);

        SymbolDel(entry->value);

        table->symbols.FreeHEntry(entry);

        return nullptr;
    }

    return entry->value;
}

Symbol *SymbolTable::Declare(const char *name, const SymbolType type, const MSize offset) noexcept {
    auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->Declare(o_name.get(), type, offset);
}

Symbol *SymbolTable::DeclareSymbolScope(ORString *name, const SymbolType type, const MSize offset, const MSize line_start) noexcept {
    auto *table = this->scope->active;

    STHEntry *entry;

    auto *sym = this->Declare(name, type, offset);
    if (sym == nullptr)
        return nullptr;

    auto *scope = this->ScopeNew(line_start);
    if (scope == nullptr) {
        if (table->symbols.Remove(sym->name, &entry)) {
            O_FAST_DECREF(entry->key);

            table->symbols.FreeHEntry(entry);

            this->SymbolDel(sym);
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
        case SymbolType::METHOD:
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

Symbol *SymbolTable::DeclareSymbolScope(const char *name, const SymbolType type, const MSize offset,
                                        const MSize line_start) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->DeclareSymbolScope(o_name.get(), type, offset, line_start);
}

Symbol *SymbolTable::Lookup(ORString *name, const MSize offset, const bool class_prop) noexcept {
    STHEntry *entry;

    const auto *scope = this->scope;

    /*
     * Prevent looking up variables inside a class/trait scope unless 'self' is used.
     * Search in class/trait scope only if the current scope is a class/trait,
     * otherwise skip class/trait scopes while climbing the hierarchy.
     */
    const auto from_clazz = scope->type == ScopeType::CLASS || scope->type == ScopeType::TRAIT || class_prop;

    while (class_prop && scope != nullptr && (scope->type != ScopeType::CLASS && scope->type != ScopeType::TRAIT))
        scope = scope->back;

    while (scope != nullptr) {
        const auto *s_scope = scope->active;

        while (s_scope != nullptr) {
            if ((from_clazz || (scope->type != ScopeType::CLASS
                                && scope->type != ScopeType::TRAIT))
                && s_scope->symbols.Lookup(name, &entry)) {
                auto *sym = entry->value;

                if (from_clazz)
                    return sym;

                if (sym->next != nullptr && sym->next->decl_offset <= offset)
                    sym = sym->next;

                if (!sym->tdz && s_scope->nesting >= sym->nesting && sym->decl_offset <= offset)
                    return sym;
            }

            s_scope = s_scope->parent;
        }

        scope = scope->back;
    }

    this->status = SymbolTableError::SYMBOL_NOT_FOUND;
    return nullptr;
}

Symbol *SymbolTable::Lookup(const char *name, const MSize offset) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->Lookup(o_name.get(), offset);
}

Symbol *SymbolTable::LookupInsert(ORString *name, const MSize offset) noexcept {
    auto *sym = this->Lookup(name, offset);
    if (sym != nullptr && sym->type != SymbolType::UNKNOWN) {
        if ((sym->type != SymbolType::FUNC && sym->type != SymbolType::VARIABLE && sym->type != SymbolType::PARAMETER)
            || sym->defining_scope == nullptr
            || this->scope == sym->defining_scope
            || sym->defining_scope->type != ScopeType::FUNCTION)
            return sym;

        if (sym->type == SymbolType::PARAMETER)
            sym->stack_offset = sym->offset;

        sym->flags |= SymbolFlags::UPVALUE;

        if (this->c_offset == nullptr)
            this->c_offset = &sym->defining_scope->closure_offset;

        sym->offset = *this->c_offset;

        *this->c_offset += 1;

        this->scope->closure = true;

        return sym;
    }

    this->status = SymbolTableError::OK;

    if (sym != nullptr)
        return sym;

    return this->Declare(name, SymbolType::UNKNOWN, offset);
}

Symbol *SymbolTable::LookupInsert(const char *name, const MSize offset) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->LookupInsert(o_name.get(), offset);
}

SymbolTable *SymbolTable::New(orbiter::Isolate *isolate) noexcept {
    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *table = allocator.alloc<SymbolTable>(sizeof(SymbolTable));
    if (table != nullptr) {
        new(table)SymbolTable(isolate);

        auto *scope = table->ScopeNew(0);
        if (scope == nullptr) {
            allocator.free(table);

            return nullptr;
        }

        scope->type = ScopeType::MODULE;

        table->scope = scope;

        table->isolate = isolate;

        table->status = SymbolTableError::OK;
    }

    return table;
}

void SymbolTable::ComputeLocalVarOffset(const SubScope *s_scope) const noexcept {
    if (s_scope->sibling != nullptr)
        this->ComputeLocalVarOffset(s_scope->sibling);

    for (auto s_cursor = s_scope; s_cursor != nullptr; s_cursor = s_cursor->next) {
        for (auto cursor = s_cursor->symbols.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
            auto *value = cursor->value;

            while (value != nullptr && ENUMBITMASK_ISFALSE(value->flags, SymbolFlags::ANON)) {
                if ((value->type == SymbolType::VARIABLE
                     || value->type == SymbolType::FUNC
                     || value->type == SymbolType::METHOD))
                    value->offset = this->scope->local_variables++;

                if (value->type != SymbolType::ECONST
                    && value->type != SymbolType::LABEL
                    && value->type != SymbolType::MODULE
                    && value->type != SymbolType::PARAMETER
                    && value->type != SymbolType::UNKNOWN) {
                    if (value->defining_scope->type != ScopeType::CLASS)
                        value->offset = this->scope->global_offset++;
                }

                if (value->type == SymbolType::UNKNOWN)
                    value->offset = this->scope->unknown_variables++;

                value = value->next;
            }
        }
    }
}

void SymbolTable::Delete(SymbolTable *table) noexcept {
    if (table == nullptr)
        return;

    const orbiter::memory::IsolateAllocator allocator(table->isolate);

    table->~SymbolTable();

    allocator.free(table);
}

void SymbolTable::LeaveNestedScope(const MSize offset) const noexcept {
    this->scope->active->offset_end = offset;

    this->scope->active = this->scope->active->parent;
}

void SymbolTable::LeaveScope(const MSize offset, const MSize line_end) noexcept {
    this->status = SymbolTableError::OK;

    auto *c_scope = this->scope;

    c_scope->active = &c_scope->sub_scope;

    c_scope->sub_scope.offset_end = offset;
    c_scope->line_end = line_end;

    this->ComputeLocalVarOffset(c_scope->active);

    if (this->c_offset == &c_scope->closure_offset)
        this->c_offset = nullptr;

    if (this->scope->type != ScopeType::MODULE) {
        this->scope = c_scope->back;

        if (this->scope->type == ScopeType::FUNCTION && this->scope->back->type == ScopeType::FUNCTION)
            this->scope->closure = c_scope->closure;
    }
}

void SymbolTable::LeaveScope() noexcept {
    this->status = SymbolTableError::OK;

    if (this->scope->type != ScopeType::MODULE)
        this->scope = this->scope->back;
}

void SymbolTable::ScopeDel(Scope *scope) const noexcept {
    const orbiter::memory::IsolateAllocator allocator(this->isolate);

    this->SubScopeDel(&scope->sub_scope, false);

    scope->~Scope();

    allocator.free(scope);
}

void SymbolTable::SymbolDel(Symbol *symbol) const noexcept {
    if (symbol == nullptr)
        return;

    const orbiter::memory::IsolateAllocator allocator(this->isolate);

    while (symbol != nullptr) {
        O_FAST_DECREF(symbol->name);

        if (symbol->scope != nullptr)
            this->ScopeDel(symbol->scope);

        auto *tmp = symbol->next;

        allocator.free(symbol);

        symbol = tmp;
    }
}

void SymbolTable::SubScopeDel(SubScope *sub_scope, const bool r_memory) const noexcept {
    sub_scope->symbols.Finalize([this](const STHEntry *entry) {
        O_FAST_DECREF(entry->key);

        this->SymbolDel(entry->value);
    });

    auto *next = sub_scope->sibling;
    for (auto cursor = next; cursor != nullptr; cursor = next) {
        next = cursor->next;

        this->SubScopeDel(cursor, true);
    }

    const orbiter::memory::IsolateAllocator allocator(this->isolate);

    if (r_memory)
        allocator.free(sub_scope);
}

SymbolTable::~SymbolTable() noexcept {
    auto *base = this->scope;

    while (base->type != ScopeType::MODULE)
        base = base->back;

    this->ScopeDel(base);
}
