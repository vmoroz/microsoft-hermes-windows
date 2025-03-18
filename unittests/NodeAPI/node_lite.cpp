// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "node_lite.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include "child_process.h"
#include "js_native_api.h"

namespace fs = std::filesystem;

// TODO: Move standalone functions to classes.

namespace node_lite {

std::string FormatString(const char* format, ...) {
  va_list args1;
  va_start(args1, format);
  va_list args2;
  va_copy(args2, args1);
  std::string result =
      std::string(std::vsnprintf(nullptr, 0, format, args1), '\0');
  va_end(args1);
  std::vsnprintf(&result[0], result.size() + 1, format, args2);
  va_end(args2);
  return result;
}

namespace {

std::string ReplaceAll(std::string str,
                       std::string_view from,
                       std::string_view to) {
  std::string result = std::move(str);
  if (from.empty()) return result;
  size_t start_pos = 0;
  while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
    result.replace(start_pos, from.length(), to);
    start_pos += to.length();  // In case 'to' contains 'from', like replacing
                               // 'x' with 'yx'
  }
  return result;
}

int32_t GetEndOfLineCount(char const* script) noexcept {
  return std::count(script, script + strlen(script), '\n');
}

char const* module_prefix = R"JS(
  'use strict';
  (function(module) {
    let exports = module.exports;
    const __filename = module.filename;
    const __dirname = module.path;
  )JS";

char const* module_suffix = R"JS(
    return module.exports;
  })({exports: {}, filename: "%s", path: "%s"});)JS";

int32_t const module_prefix_line_count = GetEndOfLineCount(module_prefix);

static std::string GetJSModuleText(std::string const& module_code,
                                   fs::path const& module_path) {
  std::string result;
  result += module_prefix;
  result += module_code;
  result += FormatString(
      module_suffix,
      ReplaceAll(module_path.string(), "\\", "\\\\").c_str(),
      ReplaceAll(module_path.parent_path().string(), "\\", "\\\\").c_str());
  return result;
}

static std::string UseSrcFilePath(std::string const& file) {
  std::string const toFind = "build\\v8build\\v8\\jsi";
  size_t pos = file.find(toFind);
  if (pos != std::string::npos) {
    return std::string(file).replace(pos, toFind.length(), "src");
  } else {
    return file;
  }
}

NodeApiRef MakeNodeApiRef(napi_env env, napi_value value) {
  napi_ref ref{};
  NODE_LITE_CALL(napi_create_reference(env, value, 1, &ref));
  return NodeApiRef(ref, NodeApiRefDeleter(env));
}

template <size_t N>
std::array<napi_value, N> GetArgs(napi_env env,
                                  napi_callback_info info,
                                  size_t min_argc = N) {
  std::array<napi_value, N> result;
  size_t argc = N;
  NODE_LITE_CALL(
      napi_get_cb_info(env, info, &argc, &result[0], nullptr, nullptr));
  if (min_argc < N) {
    NODE_LITE_ASSERT(
        argc >= min_argc,
        "Wrong number of arguments: %zu. Expected not more than %zu.",
        argc,
        min_argc);
    if (argc < N) {
      napi_value undefined = NodeApi::GetUndefined(env);
      for (size_t i = argc; i < N; ++i) {
        result[i] = undefined;
      }
    }
  } else {
    NODE_LITE_ASSERT(
        argc == N, "Wrong number of arguments: %zu. Expected: %zu", argc, N);
  }
  return result;
}

}  // namespace

void NodeApiRefDeleter::operator()(napi_ref ref) {
  NODE_LITE_CALL(napi_delete_reference(env, ref));
}

NodeApiHandleScope::NodeApiHandleScope(napi_env env) noexcept : env_(env) {
  NODE_LITE_CALL(napi_open_handle_scope(env, &handle_scope_));
}

NodeApiHandleScope::~NodeApiHandleScope() noexcept {
  napi_env env = env_;
  NODE_LITE_CALL(napi_close_handle_scope(env_, handle_scope_));
}

//=============================================================================
// NodeLiteTaskRunner implementation
//=============================================================================

uint32_t NodeLiteTaskRunner::PostTask(std::function<void()> task) {
  uint32_t task_id = next_task_id_++;
  task_queue_.emplace_back(task_id, std::move(task));
  return task_id;
}

void NodeLiteTaskRunner::RemoveTask(uint32_t task_id) noexcept {
  task_queue_.remove_if(
      [task_id](const std::pair<uint32_t, std::function<void()>>& entry) {
        return entry.first == task_id;
      });
}

void NodeLiteTaskRunner::DrainTaskQueue() {
  while (!task_queue_.empty()) {
    std::pair<uint32_t, std::function<void()>> task =
        std::move(task_queue_.front());
    task_queue_.pop_front();
    task.second();
  }
}

//=============================================================================
// NodeLiteRuntime implementation
//=============================================================================

void NodeLiteRuntime::Run(
    StringVector argv,
    std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter) {
  StringVector args = ParseArgs(argv);
  std::shared_ptr<NodeLiteTaskRunner> taskRunner =
      std::make_shared<NodeLiteTaskRunner>();

  fs::path js_file_path = fs::path(args[1]);
  fs::path js_root_dir = js_file_path.parent_path().parent_path();
  NodeLiteRuntime runtime(std::move(runtime_adapter),
                          std::move(taskRunner),
                          js_root_dir.string(),
                          std::move(args));
  NodeApiHandleScope scope{runtime.env_};

  std::string script_text =
      GetJSModuleText(ReadFileText(js_file_path), js_file_path);
  runtime.script_modules_["MainScript"] =
      NodeLiteScriptInfo{script_text.c_str(), js_file_path, 1};
  runtime.RunModuleScript(script_text.c_str());
  runtime.task_runner_->DrainTaskQueue();
  runtime.RunCallChecks();
}

NodeLiteRuntime::NodeLiteRuntime(
    std::unique_ptr<INodeLiteRuntimeAdapter> runtime_adapter,
    std::shared_ptr<NodeLiteTaskRunner> task_runner,
    std::string const& script_dir,
    std::vector<std::string> argv)
    : runtime_adapter_(std::move(runtime_adapter)),
      env_(runtime_adapter_->GetEnv()),
      script_dir_(script_dir),
      task_runner_(std::move(task_runner)),
      script_modules_(GetCommonScripts(script_dir)),
      argv_(std::move(argv)) {
  DefineGlobalFunctions();
  DefineChildProcessModule();
}

// Convert arguments to vector of strings and skip all options before the JS
// file name.
/*static*/ StringVector NodeLiteRuntime::ParseArgs(StringVector argv) noexcept {
  if (argv.size() < 2) {
    std::cerr << "Usage: " << argv[0] << " <js_file>" << std::endl;
    exit(1);
  }

  // Skip all options before the JS file name.
  StringVector args;
  args.reserve(argv.size());
  args.push_back(std::move(argv[0]));
  bool skip_options = true;
  for (int i = 1; i < argv.size(); i++) {
    std::string arg = std::move(argv[i]);
    if (skip_options && arg.find("--") == 0) {
      continue;
    }
    skip_options = false;
    args.push_back(std::move(arg));
  }

  return args;
}

std::map<std::string, NodeLiteScriptInfo, std::less<>>
NodeLiteRuntime::GetCommonScripts(std::string const& script_dir) noexcept {
  std::map<std::string, NodeLiteScriptInfo, std::less<>> module_scripts;
  module_scripts.try_emplace(
      "assert",
      NodeLiteScriptInfo{ReadScriptText(script_dir, "common/assert.js"),
                         "common/assert.js",
                         1});
  module_scripts.try_emplace(
      "../../common",
      NodeLiteScriptInfo{ReadScriptText(script_dir, "common/common.js"),
                         "common/common.js",
                         1});
  return module_scripts;
}

NodeApiRef NodeLiteRuntime::RunModuleScript(std::string const& code) noexcept {
  NodeApiHandleScope handle_scope{env_};
  return MakeNodeApiRef(
      env_, NodeApi::RunScript(env_, NodeApi::CreateString(env_, code)));
}

napi_value NodeLiteRuntime::GetModuleExports(
    napi_env env, std::string const& module_name) noexcept {
  napi_value result{};

  // Check if the module has already been initialized.
  auto module_it = initialized_modules_.find(module_name);
  if (module_it != initialized_modules_.end()) {
    return NodeApi::GetReferenceValue(env, module_it->second.get());
  }

  auto register_module = [this](std::string const& module_name,
                                NodeApiRef module_exports) -> napi_value {
    napi_env env = env_;
    if (NodeApi::IsExceptionPending(env_)) {
      return nullptr;
    }
    napi_value exports_value =
        NodeApi::GetReferenceValue(env_, module_exports.get());
    auto emplace_result = initialized_modules_.try_emplace(
        module_name, std::move(module_exports));
    NODE_LITE_ASSERT(emplace_result.second == true,
                     "Module is already registered: %s",
                     module_name.c_str());
    return exports_value;
  };

  // Check if the module is a registered script module.
  auto script_it = script_modules_.find(module_name);
  if (script_it != script_modules_.end()) {
    return register_module(
        module_name,
        RunModuleScript(GetJSModuleText(script_it->second.script,
                                        script_it->second.file_path)));
  }

  // Check if the module is registered native module.
  auto native_module_it = native_modules_.find(module_name);
  if (native_module_it != native_modules_.end()) {
    return register_module(
        module_name,
        InitializeNativeModule(NAPI_VERSION, native_module_it->second));
  }

  // Check if it is a native module.
  constexpr std::string_view native_module_prefix{"./build/x86/"};
  if (module_name.find(native_module_prefix) == 0) {
    using ModuleRegisterFuncCallback =
        napi_value(NAPI_CDECL*)(napi_env env, napi_value exports);
    using ModuleApiVersionCallback = int32_t(NAPI_CDECL*)();

    std::string dll_name = module_name.substr(native_module_prefix.size());
    HMODULE dll_module = ::LoadLibraryA(dll_name.c_str());
    if (dll_module != NULL) {
      ModuleRegisterFuncCallback module_register_func =
          reinterpret_cast<ModuleRegisterFuncCallback>(
              ::GetProcAddress(dll_module, "napi_register_module_v1"));
      ModuleApiVersionCallback get_module_api_version =
          reinterpret_cast<ModuleApiVersionCallback>(::GetProcAddress(
              dll_module, "node_api_module_get_api_version_v1"));
      if (module_register_func != nullptr) {
        int32_t module_api_version =
            get_module_api_version != nullptr ? get_module_api_version() : 8;
        return register_module(
            module_name,
            InitializeNativeModule(module_api_version, module_register_func));
      }
    }
  }

  // Check if it is a script module.
  if (module_name.find("@babel") == 0) {
    std::string script_file = module_name + ".js";
    fs::path script_path = fs::path(script_dir_) / script_file;
    return register_module(
        module_name,
        RunModuleScript(GetJSModuleText(
            ReadScriptText(script_dir_, script_file), script_path)));
  } else if (module_name.find("./") == 0 &&
             module_name.find(".js") != std::string::npos) {
    std::string script_file = "@babel/runtime/helpers" + module_name.substr(1);
    fs::path script_path = fs::path(script_dir_) / script_file;
    return register_module(
        module_name,
        RunModuleScript(GetJSModuleText(
            ReadScriptText(script_dir_, script_file), script_path)));
  }

  return NodeApi::GetUndefined(env);
}

NodeApiRef NodeLiteRuntime::InitializeNativeModule(
    int32_t api_version,
    std::function<napi_value(napi_env, napi_value)> init_module) noexcept {
  napi_env module_env = runtime_adapter_->CreateModuleEnv(api_version);
  NodeApiHandleScope handle_scope{module_env};
  // Use immediately invoked function expression (IIFE) for Node-API macros.
  return MakeNodeApiRef(env_, [&]() -> napi_value {
    napi_value exports = NodeApi::CreateObject(module_env);
    napi_value new_exports = init_module(module_env, exports);
    return (new_exports != nullptr) ? new_exports : exports;
  }());
}

NodeLiteScriptInfo* NodeLiteRuntime::GetScriptInfo(
    std::string const& module_name) {
  auto it = script_modules_.find(module_name);
  return it != script_modules_.end() ? &it->second : nullptr;
}

void NodeLiteRuntime::AddNativeModule(
    char const* module_name,
    std::function<napi_value(napi_env, napi_value)> init_module) {
  native_modules_.try_emplace(module_name, std::move(init_module));
}

std::string NodeLiteRuntime::ReadScriptText(std::string const& script_dir,
                                            std::string const& script_file) {
  return ReadFileText(script_dir + "/" + script_file);
}

std::string NodeLiteRuntime::ReadFileText(fs::path const& file_path) {
  std::string text;
  std::ifstream file_stream(file_path);
  if (file_stream) {
    std::ostringstream ss;
    ss << file_stream.rdbuf();
    text = ss.str();
  }
  return text;
}

/*static*/ NodeLiteRuntime* NodeLiteRuntime::GetRuntime(napi_env env) {
  napi_value global = NodeApi::GetGlobal(env);
  return static_cast<NodeLiteRuntime*>(NodeApi::GetValueExternal(
      env, NodeApi::GetProperty(env, global, "__NodeLiteRuntime__")));
}

void NodeLiteRuntime::DefineChildProcessModule() {
  AddNativeModule("child_process", [this](napi_env env, napi_value exports) {
    NodeApi::SetMethod(
        env_,
        exports,
        "spawnSync",
        [](napi_env env, napi_callback_info info) -> napi_value {
          std::array<napi_value, 2> args = GetArgs<2>(env, info, 1);
          std::string command = NodeApi::ToStdString(env, args[0]);
          std::vector<std::string> command_args =
              NodeApi::ToStdStringArray(env, args[1]);
          child_process::ProcessResult call_result =
              child_process::spawnSync(command, command_args);
          napi_value result = NodeApi::CreateObject(env);
          NodeApi::SetPropertyUInt32(env, result, "status", call_result.status);
          NodeApi::SetPropertyString(
              env, result, "stderr", call_result.std_error);
          NodeApi::SetPropertyString(
              env, result, "stdout", call_result.std_output);
          NodeApi::SetPropertyNull(env, result, "signal");
          return result;
        });
    return exports;
  });
}

void NodeLiteRuntime::DefineGlobalFunctions() {
  NodeApiHandleScope scope{env_};
  napi_value global = NodeApi::GetGlobal(env_);

  // Add global.global
  NodeApi::SetProperty(env_, global, "global", global);

  // Add global.__NodeLiteRuntime__
  NodeApi::SetProperty(
      env_, global, "__NodeLiteRuntime__", NodeApi::CreateExternal(env_, this));

  // global.require("module_name")
  NodeApi::SetMethod(
      env_,
      global,
      "require",
      [](napi_env env, napi_callback_info info) -> napi_value {
        std::array<napi_value, 1> args = GetArgs<1>(env, info);
        std::string module_name = NodeApi::ToStdString(env, args[0]);
        return GetRuntime(env)->GetModuleExports(env, module_name);
      });

  // global.gc()
  NodeApi::SetMethod(
      env_,
      global,
      "gc",
      [](napi_env env, napi_callback_info /*info*/) -> napi_value {
        GetRuntime(env)->runtime_adapter_->CollectGarbage();
        return nullptr;
      });

  auto set_immediate_cb = [](napi_env env, napi_callback_info info) {
    std::array<napi_value, 1> args = GetArgs<1>(env, info);
    std::shared_ptr<NodeApiRef> callback_ref =
        std::make_shared<NodeApiRef>(MakeNodeApiRef(env, args[0]));
    uint32_t task_id = GetRuntime(env)->task_runner_->PostTask(
        [env, callback_ref = std::move(callback_ref)]() {
          napi_value callback =
              NodeApi::GetReferenceValue(env, callback_ref->get());
          NODE_LITE_CALL(napi_call_function(
              env, NodeApi::GetUndefined(env), callback, 0, nullptr, nullptr));
        });
    return NodeApi::CreateUInt32(env, task_id);
  };

  // global.setImmediate()
  NodeApi::SetMethod(env_, global, "setImmediate", set_immediate_cb);

  // global.setTimeout()
  NodeApi::SetMethod(env_, global, "setTimeout", set_immediate_cb);

  // global.clearTimeout()
  NodeApi::SetMethod(env_,
                     global,
                     "clearTimeout",
                     [](napi_env env, napi_callback_info info) -> napi_value {
                       std::array<napi_value, 1> args = GetArgs<1>(env, info);
                       uint32_t task_id = NodeApi::GetValueUInt32(env, args[0]);
                       GetRuntime(env)->task_runner_->RemoveTask(task_id);
                       return nullptr;
                     });

  // global.process
  {
    napi_value process_obj = NodeApi::CreateObject(env_);
    NodeApi::SetProperty(env_, global, "process", process_obj);

    // process.argv
    NodeApi::SetPropertyStringArray(env_, process_obj, "argv", argv_);

    // process.execPath
    NodeApi::SetPropertyString(env_, process_obj, "execPath", argv_[0]);
  }

  // global.console
  {
    napi_value console_obj = NodeApi::CreateObject(env_);
    NodeApi::SetProperty(env_, global, "console", console_obj);

    // console.log()
    NodeApi::SetMethod(env_,
                       console_obj,
                       "log",
                       [](napi_env env, napi_callback_info info) -> napi_value {
                         std::array<napi_value, 1> args = GetArgs<1>(env, info);
                         std::string message =
                             NodeApi::ToStdString(env, args[0]);
                         std::cout << message << std::endl;
                         return nullptr;
                       });
  }
}

void NodeLiteRuntime::RunCallChecks() {
  napi_env env = env_;
  NodeApiHandleScope handle_scope{env};
  napi_value assert_exports = GetModuleExports(env, "assert");
  napi_value runCallChecks =
      NodeApi::GetProperty(env, assert_exports, "runCallChecks");
  NODE_LITE_CALL(napi_call_function(
      env, NodeApi::GetUndefined(env), runCallChecks, 0, nullptr, nullptr));
}

std::string NodeLiteRuntime::ProcessStack(std::string const& stack,
                                          std::string const& assert_method) {
  // Split up the stack string into an array of stack frames
  auto stackStream = std::istringstream(stack);
  std::string stackFrame;
  std::vector<std::string> stackFrames;
  while (std::getline(stackStream, stackFrame, '\n')) {
    stackFrames.push_back(std::move(stackFrame));
  }

  // Remove first and last stack frames: one is the error message
  // and another is the module root call.
  if (!stackFrames.empty()) {
    stackFrames.pop_back();
  }
  if (!stackFrames.empty()) {
    stackFrames.erase(stackFrames.begin());
  }

  std::string processedStack;
  bool assertFuncFound = false;
  std::string assertFuncPattern = assert_method + " (";
  const std::regex locationRE("(\\w+):(\\d+)");
  std::smatch locationMatch;
  for (auto const& frame : stackFrames) {
    if (assertFuncFound) {
      std::string processedFrame;
      if (std::regex_search(frame, locationMatch, locationRE)) {
        if (auto const* scriptInfo = GetScriptInfo(locationMatch[1].str())) {
          int32_t cppLine = scriptInfo->line +
                            std::stoi(locationMatch[2].str()) -
                            module_prefix_line_count - 1;
          processedFrame = locationMatch.prefix().str() +
                           UseSrcFilePath(scriptInfo->file_path.string()) +
                           ':' + std::to_string(cppLine) +
                           locationMatch.suffix().str();
        }
      }
      processedStack +=
          (!processedFrame.empty() ? processedFrame : frame) + '\n';
    } else {
      auto pos = frame.find(assertFuncPattern);
      if (pos != std::string::npos) {
        if (frame[pos - 1] == '.' || frame[pos - 1] == ' ') {
          assertFuncFound = true;
        }
      }
    }
  }

  return processedStack;
}

/*static*/ [[noreturn]] void NodeLiteErrorHandler::OnNodeApiFailed(
    napi_env env,
    napi_status error_code,
    char const* expr,
    const char* file,
    int32_t line) noexcept {
  // TODO: protect from stack overflow
  if (NodeApi::IsExceptionPending(env)) {
    napi_value error = NodeApi::GetAndClearLastException(env);
    ExitWithJSError(env, error);
  }
  // TODO: implement
  std::cerr << "NodeLiteErrorHandler::OnNodeApiFailed" << std::endl;
  exit(1);
  // FormatExitMessage(
  //     file_.c_str(), line_, "NodeLite exception", [&](std::ostream& os) {
  //       os << "Exception: NodeLiteException\n"
  //          << "     Code: " << error_code << '\n'
  //          << "  Message: " << what << '\n'
  //          << "     Expr: " << expr;
  //     });
  //  catch (std::exception const& ex) {
  //    return FormatExitMessage(
  //        file_.c_str(), line_, "C++ exception", [&](std::ostream& os) {
  //          os << "Exception thrown: " << ex.what();
  //        });
  //  }
  //  catch (...) {
  //    return FormatExitMessage(file_.c_str(), line_, "Unexpected exception");
  //  }
}

/*static*/ void NodeLiteErrorHandler::OnAssertFailed(char const* expr,
                                                     char const* message,
                                                     const char* file,
                                                     int32_t line) noexcept {
  // TODO: implement
  std::cerr << "NodeLiteErrorHandler::OnAssertFailed" << std::endl;
  exit(1);
  //// TODO: protect from stack overflow
  // if (NodeApi::IsExceptionPending(env)) {
  //   napi_value error = NodeApi::GetAndClearLastException(env);
  //   FailWithJSError(env, error);
  // }
  // FormatExitMessage(
  //     file_.c_str(), line_, "NodeLite exception", [&](std::ostream& os) {
  //       os << "Exception: NodeLiteException\n"
  //          << "     Code: " << error_code << '\n'
  //          << "  Message: " << what << '\n'
  //          << "     Expr: " << expr;
  //     });
  //// catch (std::exception const& ex) {
  ////   return FormatExitMessage(
  ////       file_.c_str(), line_, "C++ exception", [&](std::ostream& os) {
  ////         os << "Exception thrown: " << ex.what();
  ////       });
  //// }
  //// catch (...) {
  ////   return FormatExitMessage(file_.c_str(), line_, "Unexpected exception");
  //// }
}

/*static*/ [[noreturn]] void NodeLiteErrorHandler::ExitWithJSError(
    napi_env env, napi_value error) noexcept {
  // TODO: protect from stack overflow
  napi_valuetype error_value_type = NodeApi::TypeOf(env, error);
  if (error_value_type == napi_object) {
    std::string name = NodeApi::GetPropertyString(env, error, "name");
    std::string message = NodeApi::GetPropertyString(env, error, "message");
    std::string stack = NodeApi::GetPropertyString(env, error, "stack");
    if (name == "AssertionError") {
      ExitWithJSAssertError(env, error);
    }
    ExitWithMessage("file_", 1, "JavaScript error", [&](std::ostream& os) {
      os << "Exception: " << name << '\n'
         << "  Message: " << message << '\n'
         << "Callstack: " << stack;
    });
  } else {
    std::string message = NodeApi::CoerceToString(env, error);
    std::cerr << "NodeLiteErrorHandler::ExitWithJSError" << std::endl;
    std::cerr << message << std::endl;
    exit(1);
  }
}

/*static*/ [[noreturn]] void NodeLiteErrorHandler::ExitWithJSAssertError(
    napi_env env, napi_value error) noexcept {
  std::string message = NodeApi::GetPropertyString(env, error, "message");
  std::string method = NodeApi::GetPropertyString(env, error, "method");
  std::string expected = NodeApi::GetPropertyString(env, error, "expected");
  std::string actual = NodeApi::GetPropertyString(env, error, "actual");
  std::string source_file =
      NodeApi::GetPropertyString(env, error, "sourceFile");
  int32_t source_line = NodeApi::GetPropertyInt32(env, error, "sourceLine");
  std::string error_stack =
      NodeApi::GetPropertyString(env, error, "errorStack");
  if (error_stack.empty()) {
    error_stack = NodeApi::GetPropertyString(env, error, "stack");
  }
  std::string source_code = "<Source is unavailable>";
  /*if (source_file == "MainScript") {
    source_file = UseSrcFilePath(file_);
    source_code = GetSourceCodeSliceForError(sourceLine, 2);
    source_line += line_ - 1;
  } else if (source_file.empty()) {
    source_file = "<Unknown>";
  }
  */
  std::string method_name = "assert." + method;
  std::stringstream error_details;
  if (method_name != "assert.fail") {
    error_details << " Expected: " << expected << '\n'
                  << "   Actual: " << actual << '\n';
  }

  std::string processed_stack = error_stack;
  // ProcessStack(assertion_error_info->error_stack,
  //                                           assertion_error_info->method);

  ExitWithMessage(
      "File", source_line, "JavaScript assertion error", [&](std::ostream& os) {
        os << "Exception: " << "AssertionError" << '\n'
           << "   Method: " << method_name << '\n'
           << "  Message: " << message << '\n'
           << error_details.str(/*a filler for formatting*/)
           << "     File: " << source_file << ":" << source_line << '\n'
           << source_code << '\n'
           << "Callstack: " << '\n'
           << processed_stack /*   a filler for formatting    */
           << "Raw stack: " << '\n'
           << "  " << error_stack;
      });
}

/*static*/ [[noreturn]] void NodeLiteErrorHandler::ExitWithMessage(
    const std::string& file,
    int line,
    const std::string& message,
    std::function<void(std::ostream&)> get_error_details) noexcept {
  std::ostringstream details_stream;
  get_error_details(details_stream);
  std::string details = details_stream.str();
  std::cerr << "file:" << file << "\n";
  std::cerr << "line:" << line << "\n";
  std::cerr << message;
  if (!details.empty()) {
    std::cerr << "\n" << details;
  }
  std::cerr << std::endl;
  exit(1);
}

//=============================================================================
// NodeApi implementation
//=============================================================================

/*static*/ bool NodeApi::IsExceptionPending(napi_env env) noexcept {
  bool result{};
  NODE_LITE_CALL(napi_is_exception_pending(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetAndClearLastException(napi_env env) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_get_and_clear_last_exception(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetNull(napi_env env) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_get_null(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetUndefined(napi_env env) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_get_undefined(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetGlobal(napi_env env) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_get_global(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetReferenceValue(napi_env env,
                                                 napi_ref ref) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_get_reference_value(env, ref, &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateUInt32(napi_env env,
                                            std::uint32_t value) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_create_uint32(env, value, &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateString(napi_env env,
                                            std::string_view value) noexcept {
  napi_value result{};
  NODE_LITE_CALL(
      napi_create_string_utf8(env, value.data(), value.size(), &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateStringArray(
    napi_env env, std::vector<std::string> const& value) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_create_array(env, &result));

  uint32_t index = 0;
  for (const std::string& item : value) {
    NODE_LITE_CALL(
        napi_set_element(env, result, index++, CreateString(env, item)));
  }
  return result;
}

/*static*/ napi_value NodeApi::CreateObject(napi_env env) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_create_object(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateExternal(napi_env env,
                                              void* data) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_create_external(env, data, nullptr, nullptr, &result));
  return result;
}

/*static*/ int32_t NodeApi::GetValueInt32(napi_env env,
                                          napi_value value) noexcept {
  int32_t result{};
  NODE_LITE_CALL(napi_get_value_int32(env, value, &result));
  return result;
}

/*static*/ uint32_t NodeApi::GetValueUInt32(napi_env env,
                                            napi_value value) noexcept {
  uint32_t result{};
  NODE_LITE_CALL(napi_get_value_uint32(env, value, &result));
  return result;
}

/*static*/ void* NodeApi::GetValueExternal(napi_env env,
                                           napi_value value) noexcept {
  void* result{};
  NODE_LITE_CALL(napi_get_value_external(env, value, &result));
  return result;
}

/*static*/ bool NodeApi::HasProperty(napi_env env,
                                     napi_value obj,
                                     std::string_view utf8_name) noexcept {
  bool result{};
  NODE_LITE_CALL(napi_has_named_property(env, obj, utf8_name.data(), &result));
  return result;
}

/*static*/ napi_value NodeApi::GetProperty(
    napi_env env, napi_value obj, std::string_view utf8_name) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_get_named_property(env, obj, utf8_name.data(), &result));
  return result;
}

/*static*/ std::string NodeApi::GetPropertyString(
    napi_env env, napi_value obj, std::string_view utf8_name) noexcept {
  if (HasProperty(env, obj, utf8_name)) {
    return ToStdString(env, GetProperty(env, obj, utf8_name));
  } else {
    return "";
  }
}

/*static*/ int32_t NodeApi::GetPropertyInt32(
    napi_env env, napi_value obj, std::string_view utf8_name) noexcept {
  return GetValueInt32(env, GetProperty(env, obj, utf8_name));
}

/*static*/ std::string NodeApi::CoerceToString(napi_env env,
                                               napi_value value) noexcept {
  napi_value str_value;
  NODE_LITE_CALL(napi_coerce_to_string(env, value, &str_value));
  return ToStdString(env, str_value);
}

/*static*/ void NodeApi::SetProperty(napi_env env,
                                     napi_value obj,
                                     std::string_view utf8_name,
                                     napi_value value) noexcept {
  NODE_LITE_CALL(napi_set_named_property(env, obj, utf8_name.data(), value));
}

/*static*/ void NodeApi::SetPropertyUInt32(napi_env env,
                                           napi_value obj,
                                           std::string_view utf8_name,
                                           uint32_t value) noexcept {
  SetProperty(env, obj, utf8_name, CreateUInt32(env, value));
}

/*static*/ void NodeApi::SetPropertyString(napi_env env,
                                           napi_value obj,
                                           std::string_view utf8_name,
                                           std::string_view value) noexcept {
  SetProperty(env, obj, utf8_name, CreateString(env, value));
}

/*static*/ void NodeApi::SetPropertyStringArray(
    napi_env env,
    napi_value obj,
    std::string_view utf8_name,
    std::vector<std::string> const& value) noexcept {
  SetProperty(env, obj, utf8_name, CreateStringArray(env, value));
}

/*static*/ void NodeApi::SetPropertyNull(napi_env env,
                                         napi_value obj,
                                         std::string_view utf8_name) {
  SetProperty(env, obj, utf8_name, GetNull(env));
}

/*static*/ void NodeApi::SetMethod(napi_env env,
                                   napi_value obj,
                                   std::string_view utf8_name,
                                   napi_callback cb) noexcept {
  napi_value func{};
  NODE_LITE_CALL(napi_create_function(
      env, utf8_name.data(), utf8_name.size(), cb, nullptr, &func));
  NodeApi::SetProperty(env, obj, utf8_name, func);
}

/*static*/ std::string NodeApi::ToStdString(napi_env env,
                                            napi_value value) noexcept {
  size_t str_size{};
  NODE_LITE_CALL(napi_get_value_string_utf8(env, value, nullptr, 0, &str_size));
  std::string result(str_size, '\0');
  NODE_LITE_CALL(napi_get_value_string_utf8(
      env, value, &result[0], str_size + 1, nullptr));
  return result;
}

/*static*/ std::vector<std::string> NodeApi::ToStdStringArray(
    napi_env env, napi_value value) noexcept {
  std::vector<std::string> result;
  bool is_array;
  NODE_LITE_CALL(napi_is_array(env, value, &is_array));
  if (is_array) {
    uint32_t length;
    NODE_LITE_CALL(napi_get_array_length(env, value, &length));
    result.reserve(length);
    for (uint32_t i = 0; i < length; i++) {
      napi_value element;
      NODE_LITE_CALL(napi_get_element(env, value, i, &element));
      result.push_back(CoerceToString(env, element));
    }
  }
  return result;
}

/*static*/ napi_value NodeApi::RunScript(napi_env env,
                                         napi_value script) noexcept {
  napi_value result{};
  NODE_LITE_CALL(napi_run_script(env, script, &result));
  return result;
}

/*static*/ napi_valuetype NodeApi::TypeOf(napi_env env,
                                          napi_value value) noexcept {
  napi_valuetype result{};
  NODE_LITE_CALL(napi_typeof(env, value, &result));
  return result;
}

}  // namespace node_lite
