// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <gtest/gtest.h>

class BasicsTest : public ::testing::Test {};

TEST(BasicsTest, TestBasicOperations) {
  // Example test case for basic operations
  int a = 5;
  int b = 10;
  EXPECT_EQ(a + b, 15);
  EXPECT_NE(a, b);
}
