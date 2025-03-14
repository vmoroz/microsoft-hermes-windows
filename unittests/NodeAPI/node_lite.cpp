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

namespace node_lite {

namespace {

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
  EXIT_IF_FAILED(napi_create_reference(env, value, 1, &ref));
  return NodeApiRef(ref, NodeApiRefDeleter(env));
}

template <int32_t N>
std::array<napi_value, N> GetArgs(napi_env env,
                                  napi_callback_info info,
                                  size_t min_argc = N) {
  std::array<napi_value, N> result;
  size_t argc = N;
  EXIT_IF_FAILED(
      napi_get_cb_info(env, info, &argc, &result[0], nullptr, nullptr));
  if (min_argc < N) {
    EXIT_IF_FALSE(argc >= min_argc,
                  "Wrong number of arguments. Less then minimum provided.s");
    if (argc < N) {
      napi_value undefined = NodeApi::GetUndefined(env);
      for (size_t i = argc; i < N; ++i) {
        result[i] = undefined;
      }
    }
  } else {
    EXIT_IF_FALSE(argc == N, "Wrong number of arguments");
  }
  return result;
}

}  // namespace

void NodeApiRefDeleter::operator()(napi_ref ref) {
  EXIT_IF_FAILED(napi_delete_reference(env, ref));
}

NodeApiHandleScope::NodeApiHandleScope(napi_env env) noexcept : env_(env) {
  EXIT_IF_FAILED(napi_open_handle_scope(env, &handle_scope_));
}

NodeApiHandleScope::~NodeApiHandleScope() noexcept {
  napi_env env = env_;
  EXIT_IF_FAILED(napi_close_handle_scope(env_, handle_scope_));
}

//=============================================================================
// NodeLiteException implementation
//=============================================================================
#if 0
NodeLiteException::NodeLiteException(napi_env env,
                                     napi_status error_code,
                                     const char* expr) noexcept
    : error_code_(error_code), expr_(expr) {
  if (NodeApi::IsExceptionPending(env)) {
    napi_value error{};
    if (napi_get_and_clear_last_exception(env, &error) == napi_ok) {
      ApplyScriptErrorData(env, error);
    }
  }
}

NodeLiteException::NodeLiteException(napi_env env, napi_value error) noexcept {
  ApplyScriptErrorData(env, error);
}

NodeLiteException::NodeLiteException(std::string const& message,
                                     std::string const& stack) noexcept
    : error_code_(napi_generic_failure),
      what_(message),
      error_info_(std::make_shared<NodeLiteErrorInfo>(
          NodeLiteErrorInfo{"Error", message, stack})) {}

/*static*/ void NodeLiteException::Exit(napi_env env,
                                        napi_status error_code,
                                        char const* expr) noexcept {
  // TODO: implement
  abort();
}

void NodeLiteException::ApplyScriptErrorData(napi_env env, napi_value error) {
  error_info_ = std::make_shared<NodeLiteErrorInfo>();
  napi_valuetype errorType{};
  napi_typeof(env, error, &errorType);
  if (errorType == napi_object) {
    error_info_->name = NodeApi::GetPropertyString(env, error, "name");
    error_info_->message = NodeApi::GetPropertyString(env, error, "message");
    error_info_->stack = NodeApi::GetPropertyString(env, error, "stack");
    if (error_info_->name == "AssertionError") {
      assertion_error_info_ = std::make_shared<NodeLiteAssertionErrorInfo>();
      assertion_error_info_->method =
          NodeApi::GetPropertyString(env, error, "method");
      assertion_error_info_->expected =
          NodeApi::GetPropertyString(env, error, "expected");
      assertion_error_info_->actual =
          NodeApi::GetPropertyString(env, error, "actual");
      assertion_error_info_->source_file =
          NodeApi::GetPropertyString(env, error, "sourceFile");
      assertion_error_info_->source_line =
          NodeApi::GetPropertyInt32(env, error, "sourceLine");
      assertion_error_info_->error_stack =
          NodeApi::GetPropertyString(env, error, "errorStack");
      if (assertion_error_info_->error_stack.empty()) {
        assertion_error_info_->error_stack = error_info_->stack;
      }
    }
  } else {
    error_info_->message = NodeApi::CoerceToString(env, error);
  }
}
#endif

//=============================================================================
// NodeLiteErrorHandler implementation
//=============================================================================
#if 0
NodeLiteErrorHandler::NodeLiteErrorHandler(NodeLiteRuntime* runtime,
                                           std::exception_ptr const& exception,
                                           std::string script,
                                           std::string file,
                                           int32_t line,
                                           int32_t script_line_offset) noexcept
    : runtime_(runtime),
      exception_(exception),
      script_(std::move(script)),
      file_(std::move(file)),
      line_(line),
      script_line_offset_(script_line_offset) {}

NodeLiteErrorHandler::~NodeLiteErrorHandler() noexcept = default;

int NodeLiteErrorHandler::HandleAtProcessExit() noexcept {
  if (exception_) {
    try {
      std::rethrow_exception(exception_);
    } catch (NodeLiteException const& ex) {
      if (auto assertionError = ex.assertion_error_info()) {
        auto sourceFile = assertionError->source_file;
        auto sourceLine = assertionError->source_line - script_line_offset_;
        auto sourceCode = std::string("<Source is unavailable>");
        if (sourceFile == "MainScript") {
          sourceFile = UseSrcFilePath(file_);
          sourceCode = GetSourceCodeSliceForError(sourceLine, 2);
          sourceLine += line_ - 1;
        } else if (sourceFile.empty()) {
          sourceFile = "<Unknown>";
        }

        std::string methodName = "assert." + ex.assertion_error_info()->method;
        std::stringstream errorDetails;
        if (methodName != "assert.fail") {
          errorDetails << " Expected: " << ex.assertion_error_info()->expected
                       << '\n'
                       << "   Actual: " << ex.assertion_error_info()->actual
                       << '\n';
        }

        std::string processedStack =
            runtime_->ProcessStack(ex.assertion_error_info()->error_stack,
                                   ex.assertion_error_info()->method);

        return FormatExitMessage(
            file_.c_str(),
            sourceLine,
            "JavaScript assertion error",
            [&](std::ostream& os) {
              os << "Exception: " << ex.error_info()->name << '\n'
                 << "   Method: " << methodName << '\n'
                 << "  Message: " << ex.error_info()->message << '\n'
                 << errorDetails.str(/*a filler for formatting*/)
                 << "     File: " << sourceFile << ":" << sourceLine << '\n'
                 << sourceCode << '\n'
                 << "Callstack: " << '\n'
                 << processedStack /*   a filler for formatting    */
                 << "Raw stack: " << '\n'
                 << "  " << ex.assertion_error_info()->error_stack;
            });
      } else if (ex.error_info()) {
        return FormatExitMessage(
            file_.c_str(), line_, "JavaScript error", [&](std::ostream& os) {
              os << "Exception: " << ex.error_info()->name << '\n'
                 << "  Message: " << ex.error_info()->message << '\n'
                 << "Callstack: " << ex.error_info()->stack;
            });
      } else {
        return FormatExitMessage(
            file_.c_str(), line_, "NodeLite exception", [&](std::ostream& os) {
              os << "Exception: NodeLiteException\n"
                 << "     Code: " << ex.error_code() << '\n'
                 << "  Message: " << ex.what() << '\n'
                 << "     Expr: " << ex.expr();
            });
      }
    } catch (std::exception const& ex) {
      return FormatExitMessage(
          file_.c_str(), line_, "C++ exception", [&](std::ostream& os) {
            os << "Exception thrown: " << ex.what();
          });
    } catch (...) {
      return FormatExitMessage(file_.c_str(), line_, "Unexpected exception");
    }
  }
  return 0;
}

int NodeLiteErrorHandler::FormatExitMessage(
    const std::string& file, int line, const std::string& message) noexcept {
  return FormatExitMessage(
      file, line, message, [](std::ostream&) { return ""; });
}

int NodeLiteErrorHandler::FormatExitMessage(
    const std::string& file,
    int line,
    const std::string& message,
    std::function<void(std::ostream&)> getDetails) noexcept {
  std::ostringstream detailsStream;
  getDetails(detailsStream);
  std::string details = detailsStream.str();
  std::cerr << "file:" << file << "\n";
  std::cerr << "line:" << line << "\n";
  std::cerr << message;
  if (!details.empty()) {
    std::cerr << "\n" << details;
  }
  std::cerr << std::endl;
  return 1;
}

std::string NodeLiteErrorHandler::GetSourceCodeSliceForError(
    int32_t lineIndex, int32_t extra_line_count) noexcept {
  std::string sourceCode;
  auto sourceStream = std::istringstream(script_ + '\n');
  std::string sourceLine;
  int32_t currentLineIndex = 1;  // The line index is 1-based.

  while (std::getline(sourceStream, sourceLine, '\n')) {
    if (currentLineIndex > lineIndex + extra_line_count) break;
    if (currentLineIndex >= lineIndex - extra_line_count) {
      sourceCode += currentLineIndex == lineIndex ? "===> " : "     ";
      sourceCode += sourceLine;
      sourceCode += "\n";
    }
    ++currentLineIndex;
  }

  return sourceCode;
}
#endif

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
  // Use immediately invoked function expression (IIFE) for Node-API macros.
  return MakeNodeApiRef(env_, [&]() -> napi_value {
    napi_value script{};
    NODE_API_CALL(
        env_,
        napi_create_string_utf8(env_, code.c_str(), code.size(), &script));
    napi_value result{};
    NODE_API_CALL(env_, napi_run_script(env_, script, &result));
    return result;
  }());
}

napi_value NodeLiteRuntime::GetModuleExports(
    napi_env env, std::string const& module_name) noexcept {
  napi_value result{};

  // Check if the module has already been initialized.
  auto module_it = initialized_modules_.find(module_name);
  if (module_it != initialized_modules_.end()) {
    NODE_API_CALL(
        env, napi_get_reference_value(env, module_it->second.get(), &result));
    return result;
  }

  auto register_module = [this](std::string const& module_name,
                                NodeApiRef module_exports) -> napi_value {
    if (NodeApi::IsExceptionPending(env_)) {
      return nullptr;
    }
    napi_value exports_value{};
    NODE_API_CALL(
        env_,
        napi_get_reference_value(env_, module_exports.get(), &exports_value));
    auto emplace_result = initialized_modules_.try_emplace(
        module_name, std::move(module_exports));
    NODE_API_ASSERT(
        env_, emplace_result.second == true, "Failed to register module");
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
    napi_value exports{};
    NODE_API_CALL(module_env, napi_create_object(module_env, &exports));
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
          napi_value callback{};
          EXIT_IF_FAILED(
              napi_get_reference_value(env, callback_ref->get(), &callback));
          EXIT_IF_FAILED(napi_call_function(
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
}

void NodeLiteRuntime::RunCallChecks() {
  napi_env env = env_;
  NodeApiHandleScope handle_scope{env};
  napi_value assert_exports = GetModuleExports(env, "assert");
  napi_value runCallChecks{};
  EXIT_IF_FAILED(napi_get_named_property(
      env, assert_exports, "runCallChecks", &runCallChecks));
  EXIT_IF_FAILED(napi_call_function(
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

/*static*/ void NodeLiteRuntime::Fail(napi_env env,
                                      napi_status error_code,
                                      char const* expr) noexcept {
  // TODO: implement
}

//=============================================================================
// NodeApi implementation
//=============================================================================

/*static*/ bool NodeApi::IsExceptionPending(napi_env env) noexcept {
  bool result{};
  EXIT_IF_FAILED(napi_is_exception_pending(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetNull(napi_env env) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_get_null(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetUndefined(napi_env env) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_get_undefined(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::GetGlobal(napi_env env) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_get_global(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateUInt32(napi_env env,
                                            std::uint32_t value) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_create_uint32(env, value, &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateString(napi_env env,
                                            std::string_view value) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(
      napi_create_string_utf8(env, value.data(), value.size(), &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateStringArray(
    napi_env env, std::vector<std::string> const& value) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_create_array(env, &result));

  uint32_t index = 0;
  for (const std::string& item : value) {
    EXIT_IF_FAILED(
        napi_set_element(env, result, index++, CreateString(env, item)));
  }
  return result;
}

/*static*/ napi_value NodeApi::CreateObject(napi_env env) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_create_object(env, &result));
  return result;
}

/*static*/ napi_value NodeApi::CreateExternal(napi_env env,
                                              void* data) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_create_external(env, data, nullptr, nullptr, &result));
  return result;
}

/*static*/ int32_t NodeApi::GetValueInt32(napi_env env,
                                          napi_value value) noexcept {
  int32_t result{};
  EXIT_IF_FAILED(napi_get_value_int32(env, value, &result));
  return result;
}

/*static*/ uint32_t NodeApi::GetValueUInt32(napi_env env,
                                            napi_value value) noexcept {
  uint32_t result{};
  EXIT_IF_FAILED(napi_get_value_uint32(env, value, &result));
  return result;
}

/*static*/ void* NodeApi::GetValueExternal(napi_env env,
                                           napi_value value) noexcept {
  void* result{};
  EXIT_IF_FAILED(napi_get_value_external(env, value, &result));
  return result;
}

/*static*/ bool NodeApi::HasProperty(napi_env env,
                                     napi_value obj,
                                     std::string_view utf8_name) noexcept {
  bool result{};
  EXIT_IF_FAILED(napi_has_named_property(env, obj, utf8_name.data(), &result));
  return result;
}

/*static*/ napi_value NodeApi::GetProperty(
    napi_env env, napi_value obj, std::string_view utf8_name) noexcept {
  napi_value result{};
  EXIT_IF_FAILED(napi_get_named_property(env, obj, utf8_name.data(), &result));
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
  EXIT_IF_FAILED(napi_coerce_to_string(env, value, &str_value));
  return ToStdString(env, str_value);
}

/*static*/ void NodeApi::SetProperty(napi_env env,
                                     napi_value obj,
                                     std::string_view utf8_name,
                                     napi_value value) noexcept {
  EXIT_IF_FAILED(napi_set_named_property(env, obj, utf8_name.data(), value));
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
  EXIT_IF_FAILED(napi_create_function(
      env, utf8_name.data(), utf8_name.size(), cb, nullptr, &func));
  NodeApi::SetProperty(env, obj, utf8_name, func);
}

/*static*/ std::string NodeApi::ToStdString(napi_env env,
                                            napi_value value) noexcept {
  size_t str_size{};
  EXIT_IF_FAILED(napi_get_value_string_utf8(env, value, nullptr, 0, &str_size));
  std::string result(str_size, '\0');
  EXIT_IF_FAILED(napi_get_value_string_utf8(
      env, value, &result[0], str_size + 1, nullptr));
  return result;
}

/*static*/ std::vector<std::string> NodeApi::ToStdStringArray(
    napi_env env, napi_value value) noexcept {
  std::vector<std::string> result;
  bool is_array;
  EXIT_IF_FAILED(napi_is_array(env, value, &is_array));
  if (is_array) {
    uint32_t length;
    EXIT_IF_FAILED(napi_get_array_length(env, value, &length));
    result.reserve(length);
    for (uint32_t i = 0; i < length; i++) {
      napi_value element;
      EXIT_IF_FAILED(napi_get_element(env, value, i, &element));
      result.push_back(CoerceToString(env, element));
    }
  }
  return result;
}

}  // namespace node_lite
