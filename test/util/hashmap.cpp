// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <gtest/gtest.h>

#include <orbit/util/hashmap.h>

bool EqualFn(int a, int b) {
    return a == b;
}

bool EqualStrFn(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

size_t HashFn(const int key) {
    return std::hash<int>()(key);
}

size_t HashStrFn(const char *key) {
    return std::hash<std::string>{}(std::string(key));
}

using TestHashMap = HashMap<int, int, malloc, realloc, free, EqualFn, HashFn>;
using TestEntry = HEntry<int, int>;

using TestStrHashMap = HashMap<char *, int, malloc, realloc, free, EqualStrFn, HashStrFn>;
using TestStrEntry = HEntry<char *, int>;

TEST(HashMap, InitializeTest) {
    TestHashMap hashMap;
    EXPECT_TRUE(hashMap.Initialize(50, 100));
    EXPECT_EQ(hashMap.capacity, 50);
    EXPECT_EQ(hashMap.free_max, 100);
}

TEST(HashMapTest, InsertAndLookupTest) {
    TestHashMap hashMap;

    hashMap.Initialize();

    auto *entry = hashMap.AllocHEntry();

    // Create an entry
    entry->key = 42;
    entry->value = 1337;

    // Insert the entry
    EXPECT_TRUE(hashMap.Insert(entry));

    // Lookup the entry
    TestEntry *retrievedEntry;
    EXPECT_TRUE(hashMap.Lookup(entry->key, &retrievedEntry));
    EXPECT_EQ(retrievedEntry->key, 42);
    EXPECT_EQ(retrievedEntry->value, 1337);

    ASSERT_TRUE(hashMap.Remove(entry->key, &retrievedEntry));

    hashMap.FreeHEntry(retrievedEntry);
}

TEST(HashMapTest, InsertAndLookupStringTest) {
    TestStrHashMap hashMap;

    hashMap.Initialize(50, 40);

    // Create an entry
    auto *entry = hashMap.AllocHEntry();

    entry->key = strdup("test_key");
    entry->value = 1337;

    // Insert the entry
    EXPECT_TRUE(hashMap.Insert(entry));

    // Lookup the entry
    TestStrEntry *retrievedEntry;
    EXPECT_TRUE(hashMap.Lookup(entry->key, &retrievedEntry));
    EXPECT_STREQ(retrievedEntry->key, "test_key");
    EXPECT_EQ(retrievedEntry->value, 1337);

    ASSERT_TRUE(hashMap.Remove(entry->key, &retrievedEntry));

    free(retrievedEntry->key);

    hashMap.FreeHEntry(retrievedEntry);
}

TEST(HashMapTest, InsertAndRemoveTest) {
    TestHashMap hashMap;

    hashMap.Initialize();

    auto *entry = hashMap.AllocHEntry();

    entry->key = 42;
    entry->value = 1337;

    // Insert the entry
    EXPECT_TRUE(hashMap.Insert(entry));

    // Remove the entry
    TestEntry *removedEntry;
    EXPECT_TRUE(hashMap.Remove(entry->key, &removedEntry));
    EXPECT_EQ(removedEntry->key, 42);
    EXPECT_EQ(removedEntry->value, 1337);

    hashMap.FreeHEntry(removedEntry);
}

TEST(HashMapTest, ResizeTest) {
    TestHashMap hashMap;

    hashMap.Initialize(1);

    // Insert multiple entries to trigger resize
    for (int i = 0; i < 10; ++i) {
        auto *entry = hashMap.AllocHEntry();;

        ASSERT_NE(entry, nullptr);

        entry->key = i;
        entry->value = i;

        EXPECT_TRUE(hashMap.Insert(entry));
    }

    EXPECT_GT(hashMap.capacity, 1);

    // Remove the entry
    TestEntry *removedEntry;
    EXPECT_TRUE(hashMap.Remove(3, &removedEntry));
    EXPECT_EQ(removedEntry->key, 3);
    EXPECT_EQ(removedEntry->value, 3);

    hashMap.FreeHEntry(removedEntry);
}
