// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// A simple Node.js-like runtime that runs Node-API test scripts.

#pragma once
#ifndef NODE_API_TEST_NODE_LITE_H
#define NODE_API_TEST_NODE_LITE_H

#include <algorithm>
#include <cassert>
#include <exception>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define NAPI_EXPERIMENTAL
#include "../../API/hermes_node_api/node_api/js_native_api.h"

extern "C" {
#include "js-native-api/common.h"
}

// TODO: Do the fatal process exit instead.
// Crash if the condition is false.
#define CRASH_IF_FALSE(condition)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      assert(condition);                                                       \
      std::abort();                                                            \
      *((int*)nullptr) = 1;                                                    \
    }                                                                          \
  } while (false)

// Use this macro to handle Node-API function results.
// It throws NodeLiteException.
#define THROW_IF_NOT_OK(expr)                                                  \
  do {                                                                         \
    napi_status temp_status__ = (expr);                                        \
    if (temp_status__ != napi_status::napi_ok) {                               \
      throw NodeLiteException(env, temp_status__, #expr);                      \
    }                                                                          \
  } while (false)

#define EXIT_IF_FAILED(expr)                                                   \
  do {                                                                         \
    napi_status temp_status__ = (expr);                                        \
    if (temp_status__ != napi_status::napi_ok) {                               \
      NodeLiteException::Exit(env, temp_status__, #expr);                      \
    }                                                                          \
  } while (false)

// Define operator '|' to allow "or-ing" napi_property_attributes.
constexpr napi_property_attributes operator|(napi_property_attributes left,
                                             napi_property_attributes right) {
  return napi_property_attributes(static_cast<int>(left) |
                                  static_cast<int>(right));
}

namespace node_lite {

// Forward declarations
class NodeApiHandleScope;
class NodeLiteRuntime;
class NodeLiteErrorHandler;
class NodeLiteException;

// Properties from JavaScript Error object.
struct NodeLiteErrorInfo {
  std::string name;
  std::string message;
  std::string stack;
};

// Properties from JavaScript AssertionError object.
struct NodeLiteAssertionErrorInfo {
  std::string method;
  std::string expected;
  std::string actual;
  std::string source_file;
  int32_t source_line;
  std::string error_stack;
};

struct NodeLiteScriptInfo {
  std::string script;
  std::filesystem::path file_path;
  int32_t line;
};

class INodeLiteRuntimeAdapter {
 public:
  virtual ~INodeLiteRuntimeAdapter() = default;
  virtual napi_env GetEnv() = 0;
  virtual napi_env CreateModuleEnv(int32_t api_version) = 0;
};

// The exception used to propagate Node-API and script errors.
class NodeLiteException : std::exception {
 public:
  NodeLiteException() noexcept = default;

  NodeLiteException(napi_env env,
                    napi_status error_code,
                    char const* expr) noexcept;

  NodeLiteException(std::string const& message,
                    std::string const& stack) noexcept;

  NodeLiteException(napi_env env, napi_value error) noexcept;

  static void Exit(napi_env env,
                                napi_status error_code,
                                char const* expr) noexcept;

  const char* what() const noexcept override { return what_.c_str(); }

  napi_status error_code() const noexcept { return error_code_; }

  std::string const& expr() const noexcept { return expr_; }

  NodeLiteErrorInfo const* error_info() const noexcept {
    return error_info_.get();
  }

  NodeLiteAssertionErrorInfo const* assertion_error_info() const noexcept {
    return assertion_error_info_.get();
  }

 private:
  void ApplyScriptErrorData(napi_env env, napi_value error);

 private:
  napi_status error_code_{};
  std::string expr_;
  std::string what_;
  std::shared_ptr<NodeLiteErrorInfo> error_info_;
  std::shared_ptr<NodeLiteAssertionErrorInfo> assertion_error_info_;
};

// Handles the exceptions after running scripts.
class NodeLiteErrorHandler {
 public:
  NodeLiteErrorHandler(NodeLiteRuntime* runtime,
                       std::exception_ptr const& exception,
                       std::string script,
                       std::string file,
                       int32_t line,
                       int32_t script_line_offset) noexcept;
  ~NodeLiteErrorHandler() noexcept;

  int HandleAtProcessExit() noexcept;

  NodeLiteErrorHandler(NodeLiteErrorHandler const&) = delete;
  NodeLiteErrorHandler& operator=(NodeLiteErrorHandler const&) = delete;

  NodeLiteErrorHandler(NodeLiteErrorHandler&&) = default;
  NodeLiteErrorHandler& operator=(NodeLiteErrorHandler&&) = default;

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
  NodeLiteRuntime* runtime_;
  std::exception_ptr exception_;
  std::string script_;
  std::string file_;
  int32_t line_;
  int32_t script_line_offset_;
};

// Define NodeApiRef "smart pointer" for napi_ref as unique_ptr with a custom
// deleter.
class NodeApiRefDeleter {
 public:
  NodeApiRefDeleter(napi_env env) noexcept : env(env) {}

  void operator()(napi_ref ref) {
    THROW_IF_NOT_OK(napi_delete_reference(env, ref));
  }

 private:
  napi_env env;
};

using NodeApiRef = std::unique_ptr<napi_ref__, NodeApiRefDeleter>;

class NodeApiHandleScope {
 public:
  NodeApiHandleScope(napi_env env) noexcept : env_(env) {
    CRASH_IF_FALSE(napi_open_handle_scope(env, &handle_scope_) == napi_ok);
  }

  ~NodeApiHandleScope() noexcept {
    CRASH_IF_FALSE(napi_close_handle_scope(env_, handle_scope_) == napi_ok);
  }

  NodeApiHandleScope(const NodeApiHandleScope&) = delete;
  NodeApiHandleScope& operator=(const NodeApiHandleScope&) = delete;

 private:
  napi_env env_{};
  napi_handle_scope handle_scope_{};
};

class NodeLiteTaskRunner {
 public:
  uint32_t PostTask(std::function<void()> task);
  void RemoveTask(uint32_t task_id) noexcept;
  void DrainTaskQueue();

 private:
  std::list<std::pair<uint32_t, std::function<void()>>> task_queue_;
  uint32_t next_task_id_{1};
};

// The runtime to run test scripts.
class NodeLiteRuntime {
 public:
  static int32_t Run(int32_t argc,
                     char* argv[],
                     std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter);

  NodeLiteRuntime(std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter,
                  std::shared_ptr<NodeLiteTaskRunner> task_runner,
                  std::string const& script_dir,
                  std::vector<std::string> argv);

  static std::map<std::string, NodeLiteScriptInfo, std::less<>>
  GetCommonScripts(std::string const& script_dir) noexcept;

  NodeApiRef RunModuleScript(std::string const& code) noexcept;
  napi_value GetModuleExports(napi_env env,
                              std::string const& module_name) noexcept;
  NodeApiRef InitializeNativeModule(
      int32_t api_version,
      std::function<napi_value(napi_env, napi_value)> init_module) noexcept;
  NodeLiteScriptInfo* GetScriptInfo(std::string const& module_name);

  NodeLiteErrorHandler RunScript(char const* script,
                                 char const* file,
                                 int32_t line);
  NodeLiteErrorHandler RunScriptFile(NodeLiteScriptInfo const& script_info);
  NodeLiteErrorHandler RunScriptFile(std::string const& script_file);

  static std::string ReadScriptText(std::string const& script_dir,
                                    std::string const& script_file);
  static std::string ReadFileText(std::string const& filename);

  void AddNativeModule(
      char const* module_name,
      std::function<napi_value(napi_env, napi_value)> init_module);

  static std::string ToStdString(napi_env env, napi_value value);
  static std::vector<std::string> ToStdStringArray(napi_env env,
                                                   napi_value value);
  static NodeLiteRuntime* GetRuntime(napi_env env);

  void DefineObjectMethod(napi_value obj,
                          char const* func_name,
                          napi_callback cb);
  void DefineGlobalRequire(napi_value global);
  void DefineGlobalGC(napi_value global);
  void DefineGlobalSetImmediate(napi_value global);
  void DefineGlobalSetTimeout(napi_value global);
  void DefineGlobalClearTimeout(napi_value global);
  void DefineGlobalProcess(napi_value global);
  void DefineGlobalFunctions();
  void DefineChildProcessModule();

  static napi_value NAPI_CDECL SetImmediateCallback(napi_env env,
                                                    napi_callback_info info);
  static void SetUIntProperty(napi_env env,
                              napi_value obj,
                              char const* name,
                              uint32_t value);
  static void SetStrProperty(napi_env env,
                             napi_value obj,
                             char const* name,
                             std::string const& value);
  static void SetNullProperty(napi_env env, napi_value obj, char const* name);
  static napi_status GetUtf8String(napi_env env,
                                   napi_value value,
                                   std::string& result);

  void RunCallChecks();
  void HandleUnhandledPromiseRejections();

  std::string ProcessStack(std::string const& stack,
                           std::string const& assert_method);

  // The callback function to be executed after the script completion.
  uint32_t AddTask(napi_value callback) noexcept;
  void RemoveTask(uint32_t task_id) noexcept;
  void DrainTaskQueue();

  napi_value SpawnSync(std::string command, std::vector<std::string> args);

 private:
  std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter_;
  napi_env env_;
  std::string script_dir_;
  std::shared_ptr<NodeLiteTaskRunner> task_runner_;
  std::map<std::string, NodeApiRef, std::less<>> initialized_modules_;
  std::map<std::string, NodeLiteScriptInfo, std::less<>> script_modules_;
  std::map<std::string, std::function<napi_value(napi_env, napi_value)>>
      native_modules_;
  std::vector<std::string> argv_;
};

// The helper class to simplify some Node-API usage.
class NodeApi {
 public:
  static bool IsExceptionPending(napi_env env) noexcept;

  static napi_value GetProperty(napi_env env,
                                napi_value obj,
                                std::string_view utf8_name) noexcept;

  static std::string GetPropertyString(napi_env env,
                                       napi_value obj,
                                       std::string_view utf8_name) noexcept;

  static int32_t GetPropertyInt32(napi_env env,
                                  napi_value obj,
                                  std::string_view utf8_name) noexcept;

  static std::string CoerceToString(napi_env env, napi_value value) noexcept;

  static std::string ToStdString(napi_env env, napi_value value) noexcept;
};

}  // namespace node_lite

#endif  // !NODE_API_TEST_NODE_LITE_H