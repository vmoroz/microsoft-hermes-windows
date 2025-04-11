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

  ProcessResult RunScript(std::string_view script_filename) noexcept {
    return SpawnSync(node_lite_path_.string(),
                     {(basics_js_dir_ / script_filename).string()});
  }

 private:
  fs::path basics_js_dir_;
};

TEST_F(BasicsTest, TestHello) {
  ProcessResult result = RunScript("hello.js");
  ASSERT_STREQ("Hello\n", result.std_output.c_str());
}

TEST_F(BasicsTest, TestThrowString) {
  ProcessResult result = RunScript("throw_string.js");
  ASSERT_NE(result.std_error.find("Script error"), std::string::npos);
}

}  // namespace node_api_tests
