// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <gtest/gtest.h>

#include <orbit/orbiter/memory/memory.h>
#include <orbit/liftoff/scanner/sbuffer.h>

using namespace liftoff::scanner;

TEST(scanner_sbuffer, PutChar) {
    StoreBuffer buffer;

    ASSERT_TRUE(buffer.PutChar('A'));

    unsigned char *buf;
    unsigned int length = buffer.GetBuffer(&buf);

    ASSERT_EQ(length, 1);
    ASSERT_EQ(buf[0], 'A');

    orbiter::memory::Free(buf);
}

TEST(scanner_sbuffer, PutCharRepeat) {
    StoreBuffer buffer;

    ASSERT_TRUE(buffer.PutCharRepeat('B', 5));

    unsigned char *buf;
    unsigned int length = buffer.GetBuffer(&buf);
    ASSERT_EQ(length, 5);

    for (unsigned int i = 0; i < length; ++i) {
        ASSERT_EQ(buf[i], 'B');
    }

    orbiter::memory::Free(buf);
}

TEST(scanner_sbuffer, PutString) {
    const unsigned char str[] = "Hello";
    StoreBuffer buffer;

    ASSERT_TRUE(buffer.PutString(str, 5));

    unsigned char *buf;
    unsigned int length = buffer.GetBuffer(&buf);
    ASSERT_EQ(length, 5);
    ASSERT_EQ(memcmp(buf, str, 5), 0);

    orbiter::memory::Free(buf);
}

TEST(scanner_sbuffer, GetLength) {
    StoreBuffer buffer;
    ASSERT_EQ(buffer.GetLength(), 0);

    buffer.PutChar('C');
    ASSERT_EQ(buffer.GetLength(), 1);

    buffer.PutCharRepeat('D', 3);
    ASSERT_EQ(buffer.GetLength(), 4);

    unsigned char *buf;
    buffer.GetBuffer(&buf);  // Reset buffer
    orbiter::memory::Free(buf);

    ASSERT_EQ(buffer.GetLength(), 0);
}

TEST(scanner_sbuffer, Enlarge) {
    StoreBuffer buffer;
    ASSERT_TRUE(buffer.PutChar('E'));
    ASSERT_TRUE(buffer.PutCharRepeat('F', 1000));

    unsigned char *buf;
    unsigned int length = buffer.GetBuffer(&buf);

    ASSERT_EQ(length, 1001);
    ASSERT_EQ(buf[0], 'E');

    for (unsigned int i = 1; i < length; ++i) {
        ASSERT_EQ(buf[i], 'F');
    }

    orbiter::memory::Free(buf);
}
