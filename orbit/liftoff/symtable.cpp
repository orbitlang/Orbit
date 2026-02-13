// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/liftoff/symtable.h>

using namespace liftoff;
using namespace orbiter::datatype;

Scope *Scope::New(orbiter::Isolate *isolate, const MSize line_start) noexcept {
    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *scope = allocator.alloc<Scope>(sizeof(Scope));
    if (scope != nullptr) {
        new(scope)Scope(isolate, line_start);

        if (!scope->scope.symbols.Initialize()) {
            allocator.FreeObject(scope);

            return nullptr;
        }

        scope->active = &scope->scope;
    }

    return scope;
}

Symbol *SymbolTable::SymbolNew(ORString *name, const SymbolType type, const StorageLocation location,
                               const MSize offset) noexcept {
    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *symbol = allocator.calloc<Symbol>(sizeof(Symbol));
    if (symbol == nullptr) {
        this->status = SymbolTableError::MEMORY_ERROR;

        return nullptr;
    }

    symbol->next = nullptr;
    symbol->decl_scope = this->scope;
    symbol->defining_scope = nullptr;
    symbol->name = O_FAST_INCREF(name);
    symbol->decl_offset = offset;
    symbol->access = AccessModifier::PRIVATE;
    symbol->location = location;

    symbol->type = type;
    symbol->offset = 0;
    symbol->nesting = this->scope->active->nesting;

    if (type != SymbolType::VARIABLE)
        symbol->flags = SymbolFlags::INITIALIZED;

    if (location == StorageLocation::AUTO) {
        switch (this->scope->type) {
            case ScopeType::FUNCTION:
            case ScopeType::NATIVE_FUNC:
                symbol->location = StorageLocation::STACK;
                break;
            case ScopeType::MODULE:
                symbol->location = StorageLocation::MODULE;
                break;
            case ScopeType::CLASS:
            case ScopeType::TRAIT:
                symbol->location = StorageLocation::SLOTS;
                break;
        }
    }

    return symbol;
}

void SymbolTable::ComputeLocalVarOffset(const SubScope *s_scope) const noexcept {
    const auto *child = s_scope->child;

    const auto stack_count = this->scope->stack_count;

    for (auto cursor = s_scope->symbols.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
        auto *symbol = cursor->value;

        if (child != nullptr && symbol->decl_offset > child->offset.start) {
            this->ComputeLocalVarOffset(child);

            child = child->next_sibling;
        }

        if (symbol->type == SymbolType::CLASS || symbol->type == SymbolType::TRAIT) {
            symbol->offset = this->scope->static_count++;

            continue;
        }

        if (symbol->type == SymbolType::PARAMETER) {
            symbol->offset = this->scope->parameter_count++;

            continue;
        }

        if (symbol->type == SymbolType::UNKNOWN)
            symbol->location = StorageLocation::GLOBAL;

        if (ENUMBITMASK_ISTRUE(symbol->flags, SymbolFlags::CONST))
            symbol->offset = this->scope->static_count++;

        switch (symbol->location) {
            case StorageLocation::AUTO:
                break;
            case StorageLocation::CLOSURE:
                symbol->offset = this->scope->closure_count++;
                symbol->stack_offset = this->scope->stack_count++;
                break;
            case StorageLocation::GLOBAL:
                symbol->offset = this->scope->unknown_count++;
                break;
            case StorageLocation::MODULE:
            case StorageLocation::SLOTS:
                // Module and slots share the same counter since a scope cannot be both a module and a class/trait
                symbol->offset = this->scope->slot_count++;
                break;
            case StorageLocation::STACK:
                symbol->offset = this->scope->stack_count++;
                break;
        }
    }

    while (child != nullptr) {
        this->ComputeLocalVarOffset(child);

        child = child->next_sibling;
    }

    this->scope->stack_count_max = std::max(this->scope->stack_count, this->scope->stack_count_max);
    this->scope->stack_count = stack_count;
}

void SymbolTable::ScopeDel(Scope *target) const noexcept {
    // The Scope destructor is not used directly because IsolateAllocator is required to release SubScope resources.
    // The allocator is not stored as a member to minimize memory footprint.
    const orbiter::memory::IsolateAllocator allocator(this->isolate);

    assert(target->scope.next_sibling == nullptr);

    this->SubScopeDel(&target->scope, false);

    target->~Scope();

    allocator.free(target);
}

void SymbolTable::SubScopeDel(SubScope *sub_scope, const bool r_memory) const noexcept {
    sub_scope->symbols.Finalize([this](const STHEntry *entry) {
        O_FAST_DECREF(entry->key);

        this->SymbolDel(entry->value);
    });

    auto *next = sub_scope->child;
    for (auto cursor = next; cursor != nullptr; cursor = next) {
        next = cursor->next_sibling;

        this->SubScopeDel(cursor, true);
    }

    if (r_memory) {
        const orbiter::memory::IsolateAllocator allocator(this->isolate);
        allocator.free(sub_scope);
    }
}

void SymbolTable::SymbolDel(Symbol *symbol) const noexcept {
    if (symbol == nullptr)
        return;

    const orbiter::memory::IsolateAllocator allocator(this->isolate);

    while (symbol != nullptr) {
        O_FAST_DECREF(symbol->name);

        if (symbol->defining_scope != nullptr)
            this->ScopeDel(symbol->defining_scope);

        auto *tmp = symbol->next;

        allocator.free(symbol);

        symbol = tmp;
    }
}

// PUBLIC:

bool SymbolTable::CreateNestedScope(const MSize offset) noexcept {
    orbiter::memory::IsolateAllocator allocator(this->isolate);

    auto *active = this->scope->active;

    auto *sub = allocator.alloc<SubScope>(sizeof(SubScope));
    if (sub == nullptr) {
        this->status = SymbolTableError::MEMORY_ERROR;

        return false;
    }

    new(sub)SubScope(this->isolate);
    if (!sub->symbols.Initialize(kSTMapSubscopeCapacity)) {
        this->status = SymbolTableError::MEMORY_ERROR;

        allocator.free(sub);

        return false;
    }

    sub->parent = active;
    sub->offset.start = offset;
    sub->nesting = active->nesting + 1;

    if (active->child != nullptr) {
        for (auto *cursor = active->child; cursor != nullptr; cursor = cursor->next_sibling) {
            if (cursor->next_sibling == nullptr) {
                cursor->next_sibling = sub;

                break;
            }
        }
    } else
        active->child = sub;

    this->scope->active = sub;

    return true;
}

bool SymbolTable::EnterNestedScope(const MSize offset) const noexcept {
    const auto active = this->scope->active;
    SubScope *target = nullptr;

    if (offset >= active->offset.start && offset < active->offset.end) {
        bool changed = true;

        target = active;

        while (changed && target != nullptr && target->child != nullptr) {
            changed = false;

            for (auto *cursor = target->child; cursor != nullptr; cursor = cursor->next_sibling) {
                if (offset >= cursor->offset.start && offset < cursor->offset.end) {
                    target = cursor;
                    changed = true;

                    break;
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

bool SymbolTable::EnterScope(ORString *name) noexcept {
    STHEntry *entry;

    if (!this->scope->active->symbols.Lookup(name, &entry)) {
        this->status = SymbolTableError::SYMBOL_NOT_FOUND;

        return false;
    }

    if (entry->value->defining_scope == nullptr) {
        this->status = SymbolTableError::SCOPE_NOT_FOUND;

        return false;
    }

    this->scope = entry->value->defining_scope;

    return true;
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

Symbol *SymbolTable::Declare(ORString *name, const SymbolType type, const StorageLocation location,
                             const MSize offset) noexcept {
    auto *table = this->scope->active;

    STHEntry *entry;
    if (table->symbols.Lookup(name, &entry)) {
        auto *value = entry->value;

        if (value->decl_offset != offset) {
            if (value->type == SymbolType::UNKNOWN) {
                value->next = this->SymbolNew(name, type, location, offset);
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
        value->location = location;

        return value;
    }

    if ((entry = table->symbols.AllocHEntry()) == nullptr) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    entry->key = O_FAST_INCREF(name);
    entry->value = this->SymbolNew(name, type, location, offset);
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

Symbol *SymbolTable::Declare(const char *name, const SymbolType type, const StorageLocation location,
                             const MSize offset) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->Declare(o_name.get(), type, location, offset);
}

Symbol *SymbolTable::DeclareSymbolScope(ORString *name, const SymbolType type, const MSize offset,
                                        const MSize line_start) noexcept {
    auto *table = this->scope->active;

    auto *sym = this->Declare(name, type, offset);
    if (sym == nullptr)
        return nullptr;

    auto *new_scope = Scope::New(this->isolate, line_start);
    if (new_scope == nullptr) {
        STHEntry *entry;

        if (table->symbols.Remove(sym->name, &entry)) {
            O_FAST_DECREF(entry->key);

            table->symbols.FreeHEntry(entry);

            this->SymbolDel(sym);
        }

        return nullptr;
    }

    new_scope->back = this->scope;

    switch (type) {
        case SymbolType::CLASS:
            new_scope->type = ScopeType::CLASS;
            break;
        case SymbolType::TRAIT:
            new_scope->type = ScopeType::TRAIT;
            break;
        case SymbolType::FUNC:
        case SymbolType::METHOD:
            new_scope->type = ScopeType::FUNCTION;
            break;
        case SymbolType::MODULE:
            assert(false);
            break;
        case SymbolType::NATIVE_FUNC:
            new_scope->type = ScopeType::NATIVE_FUNC;
            break;
        default:
            break;
    }

    sym->defining_scope = new_scope;

    this->scope = new_scope;

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

Symbol *SymbolTable::Lookup(ORString *name, const MSize offset) noexcept {
    const auto *cursor = this->scope;

    /*
     * Prevent looking up variables inside a class/trait scope unless 'self' is used.
     * Search in class/trait scope only if the current scope is a class/trait,
     * otherwise skip class/trait scopes while climbing the hierarchy.
     */
    const auto include_members = cursor->type == ScopeType::CLASS || cursor->type == ScopeType::TRAIT;
    while (cursor != nullptr) {
        const auto *inner_scope = cursor->active;
        while (inner_scope != nullptr) {
            if (!include_members && (cursor->type == ScopeType::CLASS || cursor->type == ScopeType::TRAIT)) {
                inner_scope = inner_scope->parent;
                continue;
            }

            STHEntry *entry;
            if (inner_scope->symbols.Lookup(name, &entry)) {
                auto *sym = entry->value;

                if (include_members)
                    return sym;

                if (sym->next != nullptr && sym->next->decl_offset <= offset)
                    sym = sym->next;

                if (ENUMBITMASK_ISTRUE(sym->flags, SymbolFlags::INITIALIZED) && sym->decl_offset <= offset)
                    return sym;
            }

            inner_scope = inner_scope->parent;
        }

        cursor = cursor->back;
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
        const auto needs_closure = (sym->type == SymbolType::FUNC
                                    || sym->type == SymbolType::VARIABLE
                                    || sym->type == SymbolType::PARAMETER)
                                   && this->scope != sym->decl_scope
                                   && sym->decl_scope->type == ScopeType::FUNCTION;
        if (!needs_closure)
            return sym;

        sym->location = StorageLocation::CLOSURE;

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

Symbol *SymbolTable::LookupMember(ORString *name) {
    auto class_scope = this->scope;
    while (class_scope != nullptr
           && (class_scope->type != ScopeType::CLASS && class_scope->type != ScopeType::TRAIT))
        class_scope = class_scope->back;

    assert(class_scope != nullptr);

    const auto *inner_scope = class_scope->active;
    while (inner_scope != nullptr) {
        STHEntry *entry;
        if (inner_scope->symbols.Lookup(name, &entry))
            return entry->value;

        inner_scope = inner_scope->parent;
    }

    this->status = SymbolTableError::SYMBOL_NOT_FOUND;
    return nullptr;
}

Symbol *SymbolTable::LookupMember(const char *name) noexcept {
    const auto o_name = ORStringNew(this->isolate, name);
    if (!o_name) {
        this->status = SymbolTableError::MEMORY_ERROR;
        return nullptr;
    }

    return this->LookupMember(o_name.get());
}

SymbolTable *SymbolTable::New(orbiter::Isolate *isolate) noexcept {
    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *table = allocator.alloc<SymbolTable>(sizeof(SymbolTable));
    if (table != nullptr) {
        new(table)SymbolTable(isolate);

        table->scope = Scope::New(isolate, 0);
        if (table->scope == nullptr) {
            allocator.free(table);

            return nullptr;
        }

        table->isolate = isolate;
    }

    return table;
}

void SymbolTable::Delete(SymbolTable *table) noexcept {
    if (table == nullptr)
        return;

    const orbiter::memory::IsolateAllocator allocator(table->isolate);

    table->~SymbolTable();

    allocator.free(table);
}

void SymbolTable::LeaveNestedScope(const MSize offset) const noexcept {
    this->scope->active->offset.end = offset;

    this->scope->active = this->scope->active->parent;
}

void SymbolTable::LeaveNestedScope() const noexcept {
    this->scope->active = this->scope->active->parent;
}

void SymbolTable::LeaveScope(const MSize offset, const MSize line_end) noexcept {
    auto *current = this->scope;

    current->active = &current->scope;

    current->scope.offset.end = offset;
    current->line.end = line_end;

    this->ComputeLocalVarOffset(current->active);

    if (this->scope->type != ScopeType::MODULE)
        this->scope = current->back;

    this->status = SymbolTableError::OK;
}

void SymbolTable::LeaveScope() noexcept {
    this->status = SymbolTableError::OK;

    if (this->scope->type != ScopeType::MODULE)
        this->scope = this->scope->back;
}

SymbolTable::~SymbolTable() noexcept {
    auto *base = this->scope;

    // The current scope may not be the root scope; it must be unwound to the module level before deletion.
    while (base->type != ScopeType::MODULE)
        base = base->back;

    this->ScopeDel(base);
}
