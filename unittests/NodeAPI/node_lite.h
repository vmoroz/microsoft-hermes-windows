// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// These tests are for NAPI and should not be JS engine specific

#pragma once
#ifndef NODE_API_TEST_NODE_LITE_H
#define NODE_API_TEST_NODE_LITE_H

#include <algorithm>
#include <exception>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#define NAPI_EXPERIMENTAL
#include "../../API/hermes_node_api/node_api/js_native_api.h"

extern "C" {
#include "js-native-api/common.h"
}

// Crash if the condition is false.
#define CRASH_IF_FALSE(condition)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      *((int*)nullptr) = 1;                                                    \
      std::terminate();                                                        \
    }                                                                          \
  } while (false)

// Use this macro to handle NAPI function results in test code.
// It throws NodeLiteException that we then convert to GTest failure.
#define THROW_IF_NOT_OK(expr)                                                  \
  do {                                                                         \
    napi_status temp_status__ = (expr);                                        \
    if (temp_status__ != napi_status::napi_ok) {                               \
      throw NodeLiteException(env, temp_status__, #expr);                      \
    }                                                                          \
  } while (false)

// Define operator '|' to allow "or-ing" napi_property_attributes in tests.
constexpr napi_property_attributes operator|(napi_property_attributes left,
                                             napi_property_attributes right) {
  return napi_property_attributes(static_cast<int>(left) |
                                  static_cast<int>(right));
}

namespace node_lite {

// Forward declarations
class NodeLiteRuntime;
struct NodeApiTestErrorHandler;
class NodeLiteException;

struct IEnvHolder {
  virtual ~IEnvHolder() {}
  virtual napi_env getEnv() = 0;
};

// Properties from JavaScript Error object.
struct NodeApiErrorInfo {
  std::string Name;
  std::string Message;
  std::string Stack;
};

// Properties from JavaScript AssertionError object.
struct NodeApiAssertionErrorInfo {
  std::string Method;
  std::string Expected;
  std::string Actual;
  std::string SourceFile;
  int32_t SourceLine;
  std::string ErrorStack;
};

struct TestScriptInfo {
  std::string script;
  std::filesystem::path filePath;
  int32_t line;
};

inline int32_t GetEndOfLineCount(char const* script) noexcept {
  return std::count(script, script + strlen(script), '\n');
}

class NodeLiteTaskRunner {
 public:
  uint32_t PostTask(std::function<void()> task);
  void RemoveTask(uint32_t taskId) noexcept;
  void DrainTaskQueue();

 private:
  std::list<std::pair<uint32_t, std::function<void()>>> taskQueue_;
  uint32_t nextTaskId_{1};
};

// The exception used to propagate NAPI and script errors.
class NodeLiteException : std::exception {
 public:
  NodeLiteException() noexcept = default;

  NodeLiteException(napi_env env,
                    napi_status error_code,
                    char const* expr) noexcept;

  NodeLiteException(napi_env env, napi_value error) noexcept;

  const char* what() const noexcept override { return what_.c_str(); }

  napi_status error_code() const noexcept { return error_code_; }

  std::string const& expr() const noexcept { return expr_; }

  NodeApiErrorInfo const* error_info() const noexcept {
    return error_info_.get();
  }

  NodeApiAssertionErrorInfo const* assertion_error_info() const noexcept {
    return assertion_error_info_.get();
  }

 private:
  void ApplyScriptErrorData(napi_env env, napi_value error);
  static napi_value GetProperty(napi_env env, napi_value obj, char const* name);
  static std::string GetPropertyString(napi_env env,
                                       napi_value obj,
                                       char const* name);
  static int32_t GetPropertyInt32(napi_env env,
                                  napi_value obj,
                                  char const* name);
  static std::string CoerceToString(napi_env env, napi_value value);
  static std::string ToString(napi_env env, napi_value value);

 private:
  napi_status error_code_{};
  std::string expr_;
  std::string what_;
  std::shared_ptr<NodeApiErrorInfo> error_info_;
  std::shared_ptr<NodeApiAssertionErrorInfo> assertion_error_info_;
};

// Define NodeApiRef "smart pointer" for napi_ref as unique_ptr with a custom
// deleter.
struct NodeApiRefDeleter {
  NodeApiRefDeleter(napi_env env) noexcept : env(env) {}

  void operator()(napi_ref ref) {
    THROW_IF_NOT_OK(napi_delete_reference(env, ref));
  }

 private:
  napi_env env;
};

using NodeApiRef = std::unique_ptr<napi_ref__, NodeApiRefDeleter>;
extern NodeApiRef MakeNodeApiRef(napi_env env, napi_value value);

struct NodeApiHandleScope {
  NodeApiHandleScope(napi_env env) noexcept : m_env{env} {
    CRASH_IF_FALSE(napi_open_handle_scope(env, &m_scope) == napi_ok);
  }

  ~NodeApiHandleScope() noexcept {
    CRASH_IF_FALSE(napi_close_handle_scope(m_env, m_scope) == napi_ok);
  }

 private:
  napi_env m_env{nullptr};
  napi_handle_scope m_scope{nullptr};
};
#if 0
struct NodeApiEnvScope {
  NodeApiEnvScope(napi_env env) noexcept : m_env{env} {
    CRASH_IF_FALSE(jsr_open_napi_env_scope(env, &m_scope) == napi_ok);
  }

  ~NodeApiEnvScope() noexcept {
    if (m_env != nullptr) {
      CRASH_IF_FALSE(jsr_close_napi_env_scope(m_env, m_scope) == napi_ok);
    }
  }

  NodeApiEnvScope(NodeApiEnvScope&& other)
      : m_env(std::exchange(other.m_env, nullptr)),
        m_scope(std::exchange(other.m_scope, nullptr)) {}

  NodeApiEnvScope& operator=(NodeApiEnvScope&& other) {
    if (this != &other) {
      NodeApiEnvScope temp(std::move(*this));
      m_env = std::exchange(other.m_env, nullptr);
      m_scope = std::exchange(other.m_scope, nullptr);
    }
    return *this;
  }

  NodeApiEnvScope(const NodeApiEnvScope&) = delete;
  NodeApiEnvScope& operator=(const NodeApiEnvScope&) = delete;

 private:
  napi_env m_env{};
  jsr_napi_env_scope m_scope{};
};
#endif

// Handles the exceptions after running tests.
struct NodeApiTestErrorHandler {
  NodeApiTestErrorHandler(NodeLiteRuntime* testContext,
                          std::exception_ptr const& exception,
                          std::string&& script,
                          std::string&& file,
                          int32_t line,
                          int32_t scriptLineOffset) noexcept;
  ~NodeApiTestErrorHandler() noexcept;

  int HandleAtProcessExit() noexcept;

  NodeApiTestErrorHandler(NodeApiTestErrorHandler const&) = delete;
  NodeApiTestErrorHandler& operator=(NodeApiTestErrorHandler const&) = delete;

  NodeApiTestErrorHandler(NodeApiTestErrorHandler&&) = default;
  NodeApiTestErrorHandler& operator=(NodeApiTestErrorHandler&&) = default;

 private:
  std::string GetSourceCodeSliceForError(int32_t lineIndex,
                                         int32_t extraLineCount) noexcept;

  int FormatExitMessage(const std::string& file,
                        int line,
                        const std::string& message) noexcept;
  int FormatExitMessage(const std::string& file,
                        int line,
                        const std::string& message,
                        std::function<void(std::ostream&)> getDetails) noexcept;

 private:
  NodeLiteRuntime* m_testContext;
  std::exception_ptr m_exception;
  std::string m_script;
  std::string m_file;
  int32_t m_line;
  int32_t m_scriptLineOffset;
};

// The context to run a NAPI test.
// Some tests require interaction of multiple JS environments.
// Thus, it is more convenient to have a special NodeApiTestContext instead of
// setting the environment per test.
class NodeLiteRuntime {
 public:
  NodeLiteRuntime(napi_env env,
                  std::shared_ptr<NodeLiteTaskRunner> taskRunner,
                  std::string const& scriptDir,
                  std::vector<std::string> argv);

  static std::map<std::string, TestScriptInfo, std::less<>> GetCommonScripts(
      std::string const& scriptDir) noexcept;

  napi_value RunScript(std::string const& code,
                       char const* sourceUrl = nullptr);
  napi_value GetModule(std::string const& moduleName);
  TestScriptInfo* GetTestScriptInfo(std::string const& moduleName);

  NodeApiTestErrorHandler RunTestScript(char const* script,
                                        char const* file,
                                        int32_t line);
  NodeApiTestErrorHandler RunTestScript(TestScriptInfo const& scripInfo);
  NodeApiTestErrorHandler RunTestScript(std::string const& scriptFile);

  static std::string ReadScriptText(std::string const& testJSPath,
                                    std::string const& scriptFile);
  static std::string ReadFileText(std::string const& fileName);

  void AddNativeModule(
      char const* moduleName,
      std::function<napi_value(napi_env, napi_value)> initModule);

  void DefineObjectMethod(napi_value obj,
                          char const* funcName,
                          napi_callback cb);
  void DefineGlobalRequire(napi_value global);
  void DefineGlobalGC(napi_value global);
  void DefineGlobalSetImmediate(napi_value global);
  void DefineGlobalSetTimeout(napi_value global);
  void DefineGlobalClearTimeout(napi_value global);
  void DefineGlobalProcess(napi_value global);
  void DefineGlobalFunctions();
  void DefineChildProcessModule();

  void RunCallChecks();
  void HandleUnhandledPromiseRejections();

  std::string ProcessStack(std::string const& stack,
                           std::string const& assertMethod);

  // The callback function to be executed after the script completion.
  uint32_t AddTask(napi_value callback) noexcept;
  void RemoveTask(uint32_t taskId) noexcept;
  void DrainTaskQueue();

  napi_value SpawnSync(std::string command, std::vector<std::string> args);

 private:
  napi_env env;
  std::string scriptDir_;
  // NodeApiEnvScope m_envScope;
  NodeApiHandleScope handleScope_;
  std::shared_ptr<NodeLiteTaskRunner> taskRunner_;
  std::map<std::string, NodeApiRef, std::less<>> initializedModules_;
  std::map<std::string, TestScriptInfo, std::less<>> scriptModules_;
  std::map<std::string, std::function<napi_value(napi_env, napi_value)>>
      nativeModules_;
  std::vector<std::string> argv_;
};

}  // namespace node_lite

#endif  // !NODE_API_TEST_NODE_LITE_H