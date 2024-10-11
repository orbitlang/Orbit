// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <gtest/gtest.h>

#include <orbit/liftoff/symtable.h>

using namespace liftoff;

class SymbolTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->ctx = orbiter::ContextInit();
        this->table = SymbolTableNew(ctx);
    }

    void TearDown() override {
        SymbolTableDel(this->table);
    }

    orbiter::Context *ctx = nullptr;
    SymbolTable *table = nullptr;
};

TEST_F(SymbolTableTest, Declare) {
    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 0));
    ASSERT_TRUE(this->table->Declare("var2", SymbolType::VARIABLE, 5));
    ASSERT_TRUE(this->table->Declare("var3", SymbolType::VARIABLE, 10));

    Symbol *sym = this->table->Lookup("var1", 2);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var1") == 0);

    sym = this->table->Lookup("var2", 2);
    ASSERT_TRUE(sym == nullptr);

    sym = this->table->Lookup("var2", 6);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var2") == 0);
}

TEST_F(SymbolTableTest, DeclareSymbolScope) {
    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 0));
    ASSERT_TRUE(this->table->Declare("var2", SymbolType::VARIABLE, 5));
    ASSERT_TRUE(this->table->DeclareSymbolScope("func1", SymbolType::FUNC, 10, 5, 10) != nullptr);

    Symbol *sym = this->table->Lookup("var1", 2);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var1") == 0);

    sym = this->table->Lookup("var2", 5);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var2") == 0);

    sym = this->table->Lookup("func1", 10);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "func1") == 0);
}

TEST_F(SymbolTableTest, LookupClosure) {
    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 0));
    ASSERT_TRUE(this->table->Declare("var2", SymbolType::VARIABLE, 5));
    ASSERT_TRUE(this->table->DeclareSymbolScope("func1", SymbolType::FUNC, 10, 5, 10) != nullptr);

    Symbol *sym = this->table->Lookup("var1", 2);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var1") == 0);

    sym = this->table->Lookup("var2", 5);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var2") == 0);
    ASSERT_TRUE(sym->type == SymbolType::VARIABLE);

    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 6));
    ASSERT_TRUE(this->table->Declare("var2", SymbolType::VARIABLE, 6));

    ASSERT_TRUE(this->table->Declare("closure1", SymbolType::VARIABLE, 6));

    ASSERT_TRUE(this->table->Declare("var3", SymbolType::VARIABLE, 6));

    ASSERT_TRUE(this->table->DeclareSymbolScope("inner1", SymbolType::FUNC, 15, 7, 9) != nullptr);

    sym = this->table->Lookup("var2", 18);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "var2") == 0);
    ASSERT_TRUE(sym->type == SymbolType::VARIABLE);

    sym = this->table->LookupInsert("closure1", 18);
    ASSERT_TRUE(orbiter::datatype::ORStringCompare(sym->name, "closure1") == 0);
    ASSERT_TRUE(sym->type == SymbolType::UPVALUE);

    this->table->LeaveScope(); // Exit from Inner
    this->table->LeaveScope(); // Exit from func1
}

TEST_F(SymbolTableTest, DuplicateError) {
    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 0));
    ASSERT_FALSE(this->table->Declare("var1", SymbolType::VARIABLE, 5));

    ASSERT_EQ(this->table->last_error, liftoff::SymbolTableError::SYMBOL_ALREADY_EXISTS);
}

TEST_F(SymbolTableTest, EnterScope) {
    ASSERT_FALSE(this->table->EnterScope("func1"));
    ASSERT_EQ(this->table->last_error, liftoff::SymbolTableError::SYMBOL_NOT_FOUND);

    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 1));

    ASSERT_FALSE(this->table->EnterScope("var1"));
    ASSERT_EQ(this->table->last_error, liftoff::SymbolTableError::SCOPE_NOT_FOUND);

    ASSERT_TRUE(this->table->DeclareSymbolScope("func1", SymbolType::FUNC, 10, 5, 10) != nullptr);

    this->table->LeaveScope();

    ASSERT_TRUE(this->table->EnterScope("func1"));
}

TEST_F(SymbolTableTest, NestedScope) {
    ASSERT_TRUE(this->table->Declare("var1", SymbolType::VARIABLE, 1));

    this->table->EnterNestedScope();

    ASSERT_TRUE(this->table->Declare("var_nested", SymbolType::VARIABLE, 2));

    ASSERT_NE(this->table->Lookup("var1", 3), nullptr);
    ASSERT_NE(this->table->Lookup("var_nested", 3), nullptr);

    this->table->LeaveNestedScope();

    ASSERT_NE(this->table->Lookup("var1", 3), nullptr);
    ASSERT_EQ(this->table->Lookup("var_nested", 3), nullptr);
}