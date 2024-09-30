// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <gtest/gtest.h>

#include <orbit/orbiter/memory/memory.h>
#include <orbit/liftoff/scanner/ibuffer.h>

using namespace liftoff::scanner;

TEST(scanner_ibuffer, AppendInput) {
    InputBuffer buffer{};

    const unsigned char testData[] = "Hello, World!";
    ASSERT_TRUE(buffer.AppendInput(testData, strlen((const char *) testData)));

    while (buffer.Peek(true) != 0);

    char *line = buffer.GetCurrentLine(nullptr);
    ASSERT_STREQ(line, "Hello, World!");
    orbiter::memory::Free(line);
}

TEST(scanner_ibuffer, AppendInputMultipleTimes) {
    InputBuffer buffer;
    ASSERT_TRUE(buffer.AppendInput((const unsigned char *) "Hello, ", 7));
    ASSERT_TRUE(buffer.AppendInput((const unsigned char *) "World!", 6));

    while (buffer.Peek(true) != 0);

    char *line = buffer.GetCurrentLine(nullptr);
    ASSERT_STREQ(line, "Hello, World!");
    orbiter::memory::Free(line);
}

TEST(scanner_ibuffer, GetCurrentLine) {
    const unsigned char testData[] = "Line 1\nLine 2\nLine 3";
    InputBuffer buffer;

    buffer.AppendInput(testData, (int) strlen((const char *) testData));

    for (int i = 0; i < 6; i++)
        buffer.Peek(true);

    int length;
    char *line = buffer.GetCurrentLine(&length);
    ASSERT_STREQ(line, "Line 1");
    ASSERT_EQ(length, 6);
    orbiter::memory::Free(line);

    // Advance to next line
    buffer.Peek(true); // Skip the newline

    buffer.Peek(true); // L
    buffer.Peek(true); // i
    buffer.Peek(true); // n

    line = buffer.GetCurrentLine(&length);
    ASSERT_STREQ(line, "Lin");
    ASSERT_EQ(length, 3);
    orbiter::memory::Free(line);
}

TEST(scanner_ibuffer, PeekAndAdvance) {
    const unsigned char testData[] = "ABC";

    InputBuffer buffer;

    buffer.AppendInput(testData, strlen((const char *) testData));

    ASSERT_EQ(buffer.Peek(false), 'A');
    ASSERT_EQ(buffer.Peek(false), 'A'); // Should not advance
    ASSERT_EQ(buffer.Peek(true), 'A');
    ASSERT_EQ(buffer.Peek(true), 'B');
    ASSERT_EQ(buffer.Peek(true), 'C');
    ASSERT_EQ(buffer.Peek(true), 0); // End of buffer
}

TEST(scanner_ibuffer, ReadFile) {
    // This test requires creating a temporary file
    const char *testFileName = "test_input.txt";

    FILE *testFile = fopen(testFileName, "w");
    ASSERT_NE(testFile, nullptr);

    fprintf(testFile, "File content");
    fclose(testFile);

    InputBuffer buffer(7, 1024);

    testFile = fopen(testFileName, "r");
    ASSERT_NE(testFile, nullptr);

    int bytesRead = buffer.ReadFile(testFile);
    ASSERT_GT(bytesRead, 0);

    while (buffer.Peek(true) != 0);

    bytesRead = buffer.ReadFile(testFile);
    ASSERT_GT(bytesRead, 0);

    while (buffer.Peek(true) != 0);

    char *line = buffer.GetCurrentLine(nullptr);
    ASSERT_STREQ(line, "File content");
    orbiter::memory::Free(line);

    fclose(testFile);
    remove(testFileName);
}
