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

#define EXIT_IF_FAILED(expr)                                                   \
  do {                                                                         \
    napi_status temp_status__ = (expr);                                        \
    if (temp_status__ != napi_status::napi_ok) {                               \
      NodeLiteErrorHandler::OnNodeApiFailed(                                   \
          env, temp_status__, #expr, __FILE__, __LINE__);                      \
    }                                                                          \
  } while (false)

#define EXIT_IF_FALSE(expr, message)                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      NodeLiteErrorHandler::OnAssertFailed(                                    \
          #expr, message, __FILE__, __LINE__);                                 \
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
  virtual void CollectGarbage() = 0;
};

// Define NodeApiRef "smart pointer" for napi_ref as unique_ptr with a custom
// deleter.
class NodeApiRefDeleter {
 public:
  NodeApiRefDeleter(napi_env env) noexcept : env(env) {}
  void operator()(napi_ref ref);

 private:
  napi_env env;
};

using NodeApiRef = std::unique_ptr<napi_ref__, NodeApiRefDeleter>;

class NodeApiHandleScope {
 public:
  NodeApiHandleScope(napi_env env) noexcept;
  ~NodeApiHandleScope() noexcept;

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

class NodeLiteErrorHandler {
 public:
  static void OnNodeApiFailed(napi_env env,
                              napi_status error_code,
                              char const* expr,
                              const char* file,
                              int32_t line) noexcept;

  static void OnAssertFailed(char const* expr,
                             char const* message,
                             const char* file,
                             int32_t line) noexcept;

  static void ExitWithJSError(napi_env env, napi_value error) noexcept;

  static void ExitWithJSAssertError(napi_env env, napi_value error) noexcept;

  static void ExitWithMessage(
      const std::string& file,
      int line,
      const std::string& message,
      std::function<void(std::ostream&)> get_error_details) noexcept;
};

using StringVector = std::vector<std::string>;

// The runtime to run test scripts.
class NodeLiteRuntime {
 public:
  static void Run(StringVector argv,
                  std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter);

  NodeLiteRuntime(std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter,
                  std::shared_ptr<NodeLiteTaskRunner> task_runner,
                  std::string const& script_dir,
                  StringVector argv);

  static StringVector ParseArgs(StringVector argv) noexcept;

  static std::map<std::string, NodeLiteScriptInfo, std::less<>>
  GetCommonScripts(std::string const& script_dir) noexcept;

  NodeApiRef RunModuleScript(std::string const& code) noexcept;
  napi_value GetModuleExports(napi_env env,
                              std::string const& module_name) noexcept;
  NodeApiRef InitializeNativeModule(
      int32_t api_version,
      std::function<napi_value(napi_env, napi_value)> init_module) noexcept;
  NodeLiteScriptInfo* GetScriptInfo(std::string const& module_name);

  static std::string ReadScriptText(std::string const& script_dir,
                                    std::string const& script_file);
  static std::string ReadFileText(std::filesystem::path const& file_path);

  void AddNativeModule(
      char const* module_name,
      std::function<napi_value(napi_env, napi_value)> init_module);

  static NodeLiteRuntime* GetRuntime(napi_env env);

  void DefineGlobalFunctions();
  void DefineChildProcessModule();

  void RunCallChecks();

  std::string ProcessStack(std::string const& stack,
                           std::string const& assert_method);

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

  static napi_value GetAndClearLastException(napi_env env) noexcept;

  static napi_value GetNull(napi_env env) noexcept;

  static napi_value GetUndefined(napi_env env) noexcept;

  static napi_value GetGlobal(napi_env env) noexcept;

  static napi_value GetReferenceValue(napi_env env, napi_ref ref) noexcept;

  static napi_value CreateUInt32(napi_env env, std::uint32_t value) noexcept;

  static napi_value CreateString(napi_env env, std::string_view value) noexcept;

  static napi_value CreateStringArray(
      napi_env env, std::vector<std::string> const& value) noexcept;

  static napi_value CreateObject(napi_env env) noexcept;

  static napi_value CreateExternal(napi_env env, void* data) noexcept;

  static int32_t GetValueInt32(napi_env env, napi_value value) noexcept;

  static uint32_t GetValueUInt32(napi_env env, napi_value value) noexcept;

  static void* GetValueExternal(napi_env env, napi_value value) noexcept;

  static bool HasProperty(napi_env env,
                          napi_value obj,
                          std::string_view utf8_name) noexcept;

  static napi_value GetProperty(napi_env env,
                                napi_value obj,
                                std::string_view utf8_name) noexcept;

  static std::string GetPropertyString(napi_env env,
                                       napi_value obj,
                                       std::string_view utf8_name) noexcept;

  static int32_t GetPropertyInt32(napi_env env,
                                  napi_value obj,
                                  std::string_view utf8_name) noexcept;

  static void SetProperty(napi_env env,
                          napi_value obj,
                          std::string_view utf8_name,
                          napi_value value) noexcept;

  static void SetPropertyUInt32(napi_env env,
                                napi_value obj,
                                std::string_view utf8_name,
                                uint32_t value) noexcept;

  static void SetPropertyString(napi_env env,
                                napi_value obj,
                                std::string_view utf8_name,
                                std::string_view value) noexcept;

  static void SetPropertyStringArray(
      napi_env env,
      napi_value obj,
      std::string_view utf8_name,
      std::vector<std::string> const& value) noexcept;

  static void SetPropertyNull(napi_env env,
                              napi_value obj,
                              std::string_view utf8_name);

  static void SetMethod(napi_env env,
                        napi_value obj,
                        std::string_view utf8_name,
                        napi_callback cb) noexcept;

  static std::string CoerceToString(napi_env env, napi_value value) noexcept;

  static std::string ToStdString(napi_env env, napi_value value) noexcept;

  static std::vector<std::string> ToStdStringArray(napi_env env,
                                                   napi_value value) noexcept;

  static napi_value RunScript(napi_env env, napi_value script) noexcept;

  static napi_valuetype TypeOf(napi_env env, napi_value value) noexcept;
};

}  // namespace node_lite

#endif  // !NODE_API_TEST_NODE_LITE_H