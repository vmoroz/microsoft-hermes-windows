// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include "child_process.h"
#include "test_main.h"

namespace fs = std::filesystem;

namespace node_api_tests {

class BasicsTest : public TestFixtureBase {
 protected:
  void SetUp() override { basics_js_dir_ = js_root_dir_ / "basics"; }

  fs::path basics_js_dir_;
};

TEST_F(BasicsTest, TestBasicOperations) {
  // Example test case for basic operations
  int a = 5;
  int b = 10;
  EXPECT_EQ(a + b, 15);
  EXPECT_NE(a, b);
}

TEST_F(BasicsTest, TestHello) {
  ProcessResult result = SpawnSync(node_lite_path_.string(),
                                   {(basics_js_dir_ / "hello.js").string()});
  ASSERT_STREQ(result.std_output.c_str(), "Hello\n");
}

}  // namespace node_api_tests
