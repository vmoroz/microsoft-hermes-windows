// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "node_lite.h"
#include <windows.h>
#include <algorithm>
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

char const* module_prefix = R"(
  'use strict';
  (function(module) {
    let exports = module.exports;
    const __filename = module.filename;
    const __dirname = module.path;)"
                            "\n";

char const* module_suffix = R"(
    return module.exports;
  })({exports: {}, filename: "%s", path: "%s"});)";

int32_t const module_prefix_line_count = GetEndOfLineCount(module_prefix);

static std::string GetJSModuleText(std::string const& jsModuleCode,
                                   fs::path const& jsModulePath) {
  std::string result;
  result += module_prefix;
  result += jsModuleCode;
  result += FormatString(
      module_suffix,
      ReplaceAll(jsModulePath.string(), "\\", "\\\\").c_str(),
      ReplaceAll(jsModulePath.parent_path().string(), "\\", "\\\\").c_str());
  return result;
}

static napi_value JSRequire(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value arg0;
  void* data;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, &arg0, nullptr, &data));

  NODE_API_ASSERT(env, argc == 1, "Wrong number of arguments");

  // Extract the name of the requested module
  char moduleName[128];
  size_t copied;
  NODE_API_CALL(env,
                napi_get_value_string_utf8(
                    env, arg0, moduleName, sizeof(moduleName), &copied));

  NodeLiteRuntime* testContext = static_cast<NodeLiteRuntime*>(data);
  return testContext->GetModule(moduleName);
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
  THROW_IF_NOT_OK(napi_create_reference(env, value, 1, &ref));
  return NodeApiRef(ref, NodeApiRefDeleter(env));
}

}  // namespace

//=============================================================================
// NodeLiteException implementation
//=============================================================================

NodeLiteException::NodeLiteException(napi_env env,
                                     napi_status error_code,
                                     const char* expr) noexcept
    : error_code_(error_code), expr_(expr) {
  bool is_exception_pending;
  napi_is_exception_pending(env, &is_exception_pending);
  if (is_exception_pending) {
    napi_value error{};
    if (napi_get_and_clear_last_exception(env, &error) == napi_ok) {
      ApplyScriptErrorData(env, error);
    }
  }
}

NodeLiteException::NodeLiteException(napi_env env, napi_value error) noexcept {
  ApplyScriptErrorData(env, error);
}

void NodeLiteException::ApplyScriptErrorData(napi_env env, napi_value error) {
  error_info_ = std::make_shared<NodeLiteErrorInfo>();
  napi_valuetype errorType{};
  napi_typeof(env, error, &errorType);
  if (errorType == napi_object) {
    error_info_->name = GetPropertyString(env, error, "name");
    error_info_->message = GetPropertyString(env, error, "message");
    error_info_->stack = GetPropertyString(env, error, "stack");
    if (error_info_->name == "AssertionError") {
      assertion_error_info_ = std::make_shared<NodeLiteAssertionErrorInfo>();
      assertion_error_info_->method = GetPropertyString(env, error, "method");
      assertion_error_info_->expected =
          GetPropertyString(env, error, "expected");
      assertion_error_info_->actual = GetPropertyString(env, error, "actual");
      assertion_error_info_->source_file =
          GetPropertyString(env, error, "sourceFile");
      assertion_error_info_->source_line =
          GetPropertyInt32(env, error, "sourceLine");
      assertion_error_info_->error_stack =
          GetPropertyString(env, error, "errorStack");
      if (assertion_error_info_->error_stack.empty()) {
        assertion_error_info_->error_stack = error_info_->stack;
      }
    }
  } else {
    error_info_->message = CoerceToString(env, error);
  }
}

/*static*/ napi_value NodeLiteException::GetProperty(napi_env env,
                                                     napi_value obj,
                                                     char const* name) {
  napi_value result{};
  napi_get_named_property(env, obj, name, &result);
  return result;
}

/*static*/ std::string NodeLiteException::GetPropertyString(napi_env env,
                                                            napi_value obj,
                                                            char const* name) {
  bool hasProperty{};
  napi_has_named_property(env, obj, name, &hasProperty);
  if (hasProperty) {
    napi_value napiValue = GetProperty(env, obj, name);
    size_t valueSize{};
    napi_get_value_string_utf8(env, napiValue, nullptr, 0, &valueSize);
    std::string value(valueSize, '\0');
    napi_get_value_string_utf8(
        env, napiValue, &value[0], valueSize + 1, nullptr);
    return value;
  } else {
    return "";
  }
}

/*static*/ int32_t NodeLiteException::GetPropertyInt32(napi_env env,
                                                       napi_value obj,
                                                       char const* name) {
  napi_value napiValue = GetProperty(env, obj, name);
  int32_t value{};
  napi_get_value_int32(env, napiValue, &value);
  return value;
}

/*static*/ std::string NodeLiteException::CoerceToString(napi_env env,
                                                         napi_value value) {
  napi_value strValue;
  napi_coerce_to_string(env, value, &strValue);
  return ToString(env, strValue);
}

/*static*/ std::string NodeLiteException::ToString(napi_env env,
                                                   napi_value value) {
  size_t valueSize{};
  napi_get_value_string_utf8(env, value, nullptr, 0, &valueSize);
  std::string str(valueSize, '\0');
  napi_get_value_string_utf8(env, value, &str[0], valueSize + 1, nullptr);
  return str;
}

//=============================================================================
// NodeLiteRuntime implementation
//=============================================================================

int32_t NodeLiteRuntime::Run(int32_t argc, char** argv) {
  // Convert arguments to vector of strings and skip all options before the JS
  // file name.
  std::vector<std::string> args;
  args.reserve(argc);
  bool skipOptions = true;
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <js_file>" << std::endl;
    return 1;
  }
  args.push_back(argv[0]);
  for (int i = 1; i < argc; i++) {
    if (skipOptions && std::string_view(argv[i]).find("--") == 0) {
      continue;
    }
    skipOptions = false;
    args.push_back(argv[i]);
  }

  try {
    std::shared_ptr<NodeLiteTaskRunner> taskRunner =
        std::make_shared<NodeLiteTaskRunner>();
    // TODO: implement
    // std::unique_ptr<IEnvHolder> envHolder = CreateEnvHolder(taskRunner);
    // napi_env env = envHolder->getEnv();
    napi_env env = nullptr;  // Placeholder for the actual environment holder.

    std::string jsFilePath = args[1];
    fs::path jsPath = fs::path(jsFilePath);
    fs::path jsRootDir = jsPath.parent_path().parent_path();
    {
      NodeLiteRuntime runtime(
          env, taskRunner, jsRootDir.string(), std::move(args));
      return runtime.RunTestScript(jsFilePath).HandleAtProcessExit();
    }

    return 0;
  } catch (...) {
    return NodeLiteErrorHandler(nullptr, std::current_exception(), "", "", 0, 0)
        .HandleAtProcessExit();
  }
}

NodeLiteRuntime::NodeLiteRuntime(
    napi_env env,
    std::shared_ptr<NodeLiteTaskRunner> task_runner,
    std::string const& script_dir,
    std::vector<std::string> argv)
    : env(env),
      script_dir_(script_dir),
      handle_scope_(env),
      task_runner_(std::move(task_runner)),
      script_modules_(GetCommonScripts(script_dir)),
      argv_(std::move(argv)) {
  DefineGlobalFunctions();
  DefineChildProcessModule();
}

std::map<std::string, NodeLiteScriptInfo, std::less<>>
NodeLiteRuntime::GetCommonScripts(std::string const& scriptDir) noexcept {
  std::map<std::string, NodeLiteScriptInfo, std::less<>> module_scripts;
  module_scripts.try_emplace(
      "assert",
      NodeLiteScriptInfo{ReadScriptText(scriptDir, "common/assert.js"),
                         "common/assert.js",
                         1});
  module_scripts.try_emplace(
      "../../common",
      NodeLiteScriptInfo{ReadScriptText(scriptDir, "common/common.js"),
                         "common/common.js",
                         1});
  return module_scripts;
}

napi_value NodeLiteRuntime::RunScript(std::string const& code,
                                      char const* sourceUrl) {
#if 0
  napi_value script{}, scriptResult{};
  THROW_IF_NOT_OK(
      napi_create_string_utf8(env, code.c_str(), code.size(), &script));
  if (sourceUrl) {
    THROW_IF_NOT_OK(jsr_run_script(env, script, sourceUrl, &scriptResult));
  } else {
    THROW_IF_NOT_OK(napi_run_script(env, script, &scriptResult));
  }
  return scriptResult;
#endif
  return nullptr;
}

using ModuleRegisterFuncCallback = napi_value(NAPI_CDECL*)(napi_env env,
                                                           napi_value exports);
using ModuleApiVersionCallback = int32_t(NAPI_CDECL*)();

napi_value NodeLiteRuntime::GetModule(std::string const& moduleName) {
  napi_value result{};

  // Check if the module has already been initialized.
  auto moduleIt = initialized_modules_.find(moduleName);
  if (moduleIt != initialized_modules_.end()) {
    NODE_API_CALL(
        env, napi_get_reference_value(env, moduleIt->second.get(), &result));
    return result;
  }

  auto registerModule = [this](napi_env env,
                               std::string const& moduleName,
                               napi_value module) {
    initialized_modules_.try_emplace(moduleName, MakeNodeApiRef(env, module));
    return module;
  };

  // Check if the module is registered script module.
  auto scriptIt = script_modules_.find(moduleName);
  if (scriptIt != script_modules_.end()) {
    return registerModule(env,
                          moduleName,
                          RunScript(GetJSModuleText(scriptIt->second.script,
                                                    scriptIt->second.file_path),
                                    moduleName.c_str()));
  }

  // Check if the module is registered native module.
  auto nativeModuleIt = native_modules_.find(moduleName);
  if (nativeModuleIt != native_modules_.end()) {
    napi_value exports{};
    NODE_API_CALL(env, napi_create_object(env, &exports));
    return registerModule(
        env, moduleName, nativeModuleIt->second(env, exports));
  }

  // Check if it is a native module.
  const char nativeModulePrefix[] = "./build/x86/";
  if (moduleName.find(nativeModulePrefix) == 0) {
    std::string dllName = moduleName.substr(std::size(nativeModulePrefix) - 1);
    HMODULE dllModule = ::LoadLibraryA(dllName.c_str());
    if (dllModule != NULL) {
      ModuleRegisterFuncCallback moduleRegisterFunc =
          reinterpret_cast<ModuleRegisterFuncCallback>(
              ::GetProcAddress(dllModule, "napi_register_module_v1"));
      ModuleApiVersionCallback getModuleApiVersion =
          reinterpret_cast<ModuleApiVersionCallback>(::GetProcAddress(
              dllModule, "node_api_module_get_api_version_v1"));
      if (moduleRegisterFunc != nullptr) {
        int32_t moduleApiVersion =
            getModuleApiVersion ? getModuleApiVersion() : 8;
        napi_env moduleEnv{};
#if 0
        NODE_API_CALL(
            env, jsr_create_node_api_env(env, moduleApiVersion, &moduleEnv));
        napi_value exports{};
        NODE_API_CALL(moduleEnv, napi_create_object(moduleEnv, &exports));

        auto task = [moduleEnv, moduleRegisterFunc, &exports]() {
          exports = moduleRegisterFunc(moduleEnv, exports);
        };
        using Task = decltype(task);
        NODE_API_CALL(
            moduleEnv,
            jsr_run_task(
                moduleEnv,
                [](void* data) { (*reinterpret_cast<Task*>(data))(); },
                &task));
        return registerModule(moduleEnv, moduleName, exports);
#endif
        return registerModule(moduleEnv, moduleName, nullptr);
      }
    }
  }

  // Check if it is a script module.
  if (moduleName.find("@babel") == 0) {
    std::string scriptFile = moduleName + ".js";
    fs::path scriptPath = fs::path(script_dir_) / scriptFile;
    return registerModule(
        env,
        moduleName,
        RunScript(GetJSModuleText(ReadScriptText(script_dir_, scriptFile),
                                  scriptPath),
                  scriptFile.c_str()));
  } else if (moduleName.find("./") == 0 &&
             moduleName.find(".js") != std::string::npos) {
    std::string scriptFile = "@babel/runtime/helpers" + moduleName.substr(1);
    fs::path scriptPath = fs::path(script_dir_) / scriptFile;
    return registerModule(
        env,
        moduleName,
        RunScript(GetJSModuleText(ReadScriptText(script_dir_, scriptFile),
                                  scriptPath),
                  scriptFile.c_str()));
  }

  NODE_API_CALL(env, napi_get_undefined(env, &result));
  return result;
}

NodeLiteScriptInfo* NodeLiteRuntime::GetTestScriptInfo(
    std::string const& moduleName) {
  auto it = script_modules_.find(moduleName);
  return it != script_modules_.end() ? &it->second : nullptr;
}

void NodeLiteRuntime::AddNativeModule(
    char const* moduleName,
    std::function<napi_value(napi_env, napi_value)> initModule) {
  native_modules_.try_emplace(moduleName, std::move(initModule));
}

NodeLiteErrorHandler NodeLiteRuntime::RunTestScript(char const* script,
                                                    char const* file,
                                                    int32_t line) {
  try {
    std::string scriptText = GetJSModuleText(script, file);
    script_modules_["TestScript"] =
        NodeLiteScriptInfo{scriptText.c_str(), file, line};

    NodeApiHandleScope scope{env};
    {
      NodeApiHandleScope scope{env};
      RunScript(scriptText.c_str(), "TestScript");
    }
    DrainTaskQueue();
    RunCallChecks();
    HandleUnhandledPromiseRejections();
    return NodeLiteErrorHandler(this, std::exception_ptr(), "", file, line, 0);
  } catch (...) {
    return NodeLiteErrorHandler(this,
                                std::current_exception(),
                                script,
                                file,
                                line,
                                module_prefix_line_count);
  }
}

void NodeLiteRuntime::HandleUnhandledPromiseRejections() {
  bool hasException{false};
#if 0
  THROW_IF_NOT_OK(jsr_has_unhandled_promise_rejection(env, &hasException));
  if (hasException) {
    napi_value error{};
    THROW_IF_NOT_OK(
        jsr_get_and_clear_last_unhandled_promise_rejection(env, &error));
    throw NodeLiteException(env, error);
  }
#endif
}

NodeLiteErrorHandler NodeLiteRuntime::RunTestScript(
    NodeLiteScriptInfo const& scriptInfo) {
  return RunTestScript(scriptInfo.script.c_str(),
                       scriptInfo.file_path.string().c_str(),
                       scriptInfo.line);
}

NodeLiteErrorHandler NodeLiteRuntime::RunTestScript(
    std::string const& scriptFile) {
  return RunTestScript(ReadFileText(scriptFile).c_str(), scriptFile.c_str(), 1);
}

std::string NodeLiteRuntime::ReadScriptText(std::string const& testJSPath,
                                            std::string const& scriptFile) {
  return ReadFileText(testJSPath + "/" + scriptFile);
}

std::string NodeLiteRuntime::ReadFileText(std::string const& fileName) {
  std::string text;
  std::ifstream fileStream(fileName);
  if (fileStream) {
    std::ostringstream ss;
    ss << fileStream.rdbuf();
    text = ss.str();
  }
  return text;
}

void NodeLiteRuntime::DefineObjectMethod(napi_value obj,
                                         char const* funcName,
                                         napi_callback cb) {
  napi_value func{};
  THROW_IF_NOT_OK(
      napi_create_function(env, funcName, NAPI_AUTO_LENGTH, cb, this, &func));
  THROW_IF_NOT_OK(napi_set_named_property(env, obj, funcName, func));
}

// global.require("module_name")
void NodeLiteRuntime::DefineGlobalRequire(napi_value global) {
  DefineObjectMethod(global, "require", JSRequire);
}

// global.gc()
void NodeLiteRuntime::DefineGlobalGC(napi_value global) {
#if 0
  DefineObjectMethod(
      global,
      "gc",
      [](napi_env env, napi_callback_info /*info*/) -> napi_value {
        NODE_API_CALL(env, jsr_collect_garbage(env));
        return nullptr;
      });
#endif
}

static napi_value NAPI_CDECL SetImmediateCallback(napi_env env,
                                                  napi_callback_info info) {
  size_t argc{1};
  napi_value immediateCallback{};
  NODE_API_CALL(
      env,
      napi_get_cb_info(env, info, &argc, &immediateCallback, nullptr, nullptr));

  // TODO: use a different macro that does not throw
  NODE_API_ASSERT(env,
                  argc >= 1,
                  "Wrong number of arguments. Expects at least one argument.");
  napi_valuetype immediateCallbackType;
  NODE_API_CALL(env,
                napi_typeof(env, immediateCallback, &immediateCallbackType));
  NODE_API_ASSERT(env,
                  immediateCallbackType == napi_function,
                  "Wrong type of arguments. Expects a function.");

  napi_value global{};
  NODE_API_CALL(env, napi_get_global(env, &global));
  napi_value selfValue{};
  NODE_API_CALL(env,
                napi_get_named_property(
                    env, global, "__NodeApiTestContext__", &selfValue));
  NodeLiteRuntime* self;
  NODE_API_CALL(env, napi_get_value_external(env, selfValue, (void**)&self));

  uint32_t task_id = self->AddTask(immediateCallback);

  napi_value taskIdValue{};
  NODE_API_CALL(env, napi_create_uint32(env, task_id, &taskIdValue));
  return taskIdValue;
}

// global.setImmediate()
void NodeLiteRuntime::DefineGlobalSetImmediate(napi_value global) {
  DefineObjectMethod(global, "setImmediate", SetImmediateCallback);
}

// global.setTimeout()
void NodeLiteRuntime::DefineGlobalSetTimeout(napi_value global) {
  DefineObjectMethod(global, "setTimeout", SetImmediateCallback);
}

// global.clearTimeout()
void NodeLiteRuntime::DefineGlobalClearTimeout(napi_value global) {
  DefineObjectMethod(
      global,
      "clearTimeout",
      [](napi_env env, napi_callback_info info) -> napi_value {
        size_t argc{1};
        napi_value taskIdValue{};
        NODE_API_CALL(
            env,
            napi_get_cb_info(env, info, &argc, &taskIdValue, nullptr, nullptr));

        NODE_API_ASSERT(
            env,
            argc >= 1,
            "Wrong number of arguments. Expects at least one argument.");
        napi_valuetype taskIdType;
        NODE_API_CALL(env, napi_typeof(env, taskIdValue, &taskIdType));
        NODE_API_ASSERT(env,
                        taskIdType == napi_number,
                        "Wrong type of argument. Expects a number.");
        uint32_t task_id;
        NODE_API_CALL(env, napi_get_value_uint32(env, taskIdValue, &task_id));

        napi_value global{};
        NODE_API_CALL(env, napi_get_global(env, &global));
        napi_value selfValue{};
        NODE_API_CALL(env,
                      napi_get_named_property(
                          env, global, "__NodeApiTestContext__", &selfValue));
        NodeLiteRuntime* self;
        NODE_API_CALL(env,
                      napi_get_value_external(env, selfValue, (void**)&self));

        self->RemoveTask(task_id);

        napi_value undefined{};
        NODE_API_CALL(env, napi_get_undefined(env, &undefined));
        return undefined;
      });
}

static std::string ToStdString(napi_env env, napi_value value) {
  napi_valuetype valueType;
  THROW_IF_NOT_OK(napi_typeof(env, value, &valueType));
  NODE_API_ASSERT(env,
                  valueType == napi_string,
                  "Wrong type of argument. Expects a string.");
  size_t valueSize{};
  napi_get_value_string_utf8(env, value, nullptr, 0, &valueSize);
  std::string str(valueSize, '\0');
  napi_get_value_string_utf8(env, value, &str[0], valueSize + 1, nullptr);
  return str;
}

static std::vector<std::string> ToStdStringArray(napi_env env,
                                                 napi_value value) {
  std::vector<std::string> result;
  bool isArray;
  THROW_IF_NOT_OK(napi_is_array(env, value, &isArray));
  if (isArray) {
    uint32_t length;
    THROW_IF_NOT_OK(napi_get_array_length(env, value, &length));
    result.reserve(length);
    for (uint32_t i = 0; i < length; i++) {
      napi_value element;
      THROW_IF_NOT_OK(napi_get_element(env, value, i, &element));
      result.push_back(ToStdString(env, element));
    }
  }
  return result;
}

static NodeLiteRuntime* GetTestContext(napi_env env) {
  napi_value global{};
  NODE_API_CALL(env, napi_get_global(env, &global));
  napi_value contextValue{};
  NODE_API_CALL(env,
                napi_get_named_property(
                    env, global, "__NodeApiTestContext__", &contextValue));
  NodeLiteRuntime* context{};
  NODE_API_CALL(env,
                napi_get_value_external(env, contextValue, (void**)&context));
  return context;
}

// global.process
void NodeLiteRuntime::DefineGlobalProcess(napi_value global) {
  napi_value processObject{};
  THROW_IF_NOT_OK(napi_create_object(env, &processObject));
  THROW_IF_NOT_OK(
      napi_set_named_property(env, global, "process", processObject));

  // process.argv
  napi_value argvArray{};
  THROW_IF_NOT_OK(napi_create_array(env, &argvArray));
  THROW_IF_NOT_OK(
      napi_set_named_property(env, processObject, "argv", argvArray));

  uint32_t index = 0;
  for (const std::string& arg : argv_) {
    napi_value argValue{};
    THROW_IF_NOT_OK(
        napi_create_string_utf8(env, arg.c_str(), arg.size(), &argValue));
    THROW_IF_NOT_OK(napi_set_element(env, argvArray, index++, argValue));
  }

  // process.execPath
  napi_value execPath{};
  THROW_IF_NOT_OK(napi_create_string_utf8(
      env, argv_[0].c_str(), argv_[0].size(), &execPath));
  THROW_IF_NOT_OK(
      napi_set_named_property(env, processObject, "execPath", execPath));
}

void NodeLiteRuntime::DefineChildProcessModule() {
  AddNativeModule("child_process", [this](napi_env env, napi_value exports) {
    DefineObjectMethod(
        exports,
        "spawnSync",
        [](napi_env env, napi_callback_info info) -> napi_value {
          size_t argc{2};
          napi_value argv[2] = {};
          NODE_API_CALL(
              env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

          NODE_API_ASSERT(
              env,
              argc >= 1,
              "Wrong number of arguments. Expects at least one argument.");
          std::string command = ToStdString(env, argv[0]);
          std::vector<std::string> args = ToStdStringArray(env, argv[1]);

          NodeLiteRuntime* self = GetTestContext(env);
          return self->SpawnSync(command, args);
        });
    return exports;
  });
}

void NodeLiteRuntime::DefineGlobalFunctions() {
  NodeApiHandleScope scope{env};

  napi_value global{};
  THROW_IF_NOT_OK(napi_get_global(env, &global));

  // Add global
  THROW_IF_NOT_OK(napi_set_named_property(env, global, "global", global));

  // Add __NodeApiTestContext__
  napi_value self{};
  THROW_IF_NOT_OK(napi_create_external(env, this, nullptr, nullptr, &self));
  THROW_IF_NOT_OK(
      napi_set_named_property(env, global, "__NodeApiTestContext__", self));

  DefineGlobalRequire(global);
  DefineGlobalGC(global);
  DefineGlobalSetImmediate(global);
  DefineGlobalSetTimeout(global);
  DefineGlobalClearTimeout(global);
  DefineGlobalProcess(global);
}

static void SetUIntProperty(napi_env env,
                            napi_value obj,
                            char const* name,
                            uint32_t value) {
  napi_value valueObj{};
  THROW_IF_NOT_OK(napi_create_uint32(env, value, &valueObj));
  THROW_IF_NOT_OK(napi_set_named_property(env, obj, name, valueObj));
}

static void SetStrProperty(napi_env env,
                           napi_value obj,
                           char const* name,
                           std::string const& value) {
  napi_value valueObj{};
  THROW_IF_NOT_OK(
      napi_create_string_utf8(env, value.c_str(), value.size(), &valueObj));
  THROW_IF_NOT_OK(napi_set_named_property(env, obj, name, valueObj));
}

static void SetNullProperty(napi_env env, napi_value obj, char const* name) {
  napi_value valueObj{};
  THROW_IF_NOT_OK(napi_get_null(env, &valueObj));
  THROW_IF_NOT_OK(napi_set_named_property(env, obj, name, valueObj));
}

napi_value NodeLiteRuntime::SpawnSync(std::string command,
                                      std::vector<std::string> args) {
  child_process::ProcessResult procResult =
      child_process::spawnSync(command, args);
  napi_value result{};
  THROW_IF_NOT_OK(napi_create_object(env, &result));
  SetUIntProperty(env, result, "status", procResult.status);
  SetStrProperty(env, result, "stderr", procResult.std_error);
  SetStrProperty(env, result, "stdout", procResult.std_output);
  SetNullProperty(env, result, "signal");
  return result;
}

uint32_t NodeLiteRuntime::AddTask(napi_value callback) noexcept {
  std::shared_ptr<NodeApiRef> ref =
      std::make_shared<NodeApiRef>(MakeNodeApiRef(env, callback));
#if 0
  return m_taskRunner->PostTask([env = this->env, ref = std::move(ref)]() {
    napi_value callback{}, undefined{};
    THROW_IF_NOT_OK(napi_get_undefined(env, &undefined));
    THROW_IF_NOT_OK(napi_get_reference_value(env, ref->get(), &callback));
    THROW_IF_NOT_OK(
        napi_call_function(env, undefined, callback, 0, nullptr, nullptr));
  });
#endif
  return 0;
}

void NodeLiteRuntime::RemoveTask(uint32_t task_id) noexcept {
  task_runner_->RemoveTask(task_id);
}

void NodeLiteRuntime::DrainTaskQueue() {
  task_runner_->DrainTaskQueue();
}

void NodeLiteRuntime::RunCallChecks() {
  napi_value common = GetModule("assert");
  napi_value undefined{}, runCallChecks{};
  THROW_IF_NOT_OK(
      napi_get_named_property(env, common, "runCallChecks", &runCallChecks));
  THROW_IF_NOT_OK(napi_get_undefined(env, &undefined));
  THROW_IF_NOT_OK(
      napi_call_function(env, undefined, runCallChecks, 0, nullptr, nullptr));
}

std::string NodeLiteRuntime::ProcessStack(std::string const& stack,
                                          std::string const& assertMethod) {
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
  std::string assertFuncPattern = assertMethod + " (";
  const std::regex locationRE("(\\w+):(\\d+)");
  std::smatch locationMatch;
  for (auto const& frame : stackFrames) {
    if (assertFuncFound) {
      std::string processedFrame;
      if (std::regex_search(frame, locationMatch, locationRE)) {
        if (auto const* scriptInfo =
                GetTestScriptInfo(locationMatch[1].str())) {
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
// NodeLiteErrorHandler implementation
//=============================================================================

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
        if (sourceFile == "TestScript") {
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
        return FormatExitMessage(file_.c_str(),
                                 line_,
                                 "Test native exception",
                                 [&](std::ostream& os) {
                                   os << "Exception: NodeLiteException\n"
                                      << "     Code: " << ex.error_code()
                                      << '\n'
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
      return FormatExitMessage(
          file_.c_str(), line_, "Unexpected test exception");
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
    int32_t lineIndex, int32_t extraLineCount) noexcept {
  std::string sourceCode;
  auto sourceStream = std::istringstream(script_ + '\n');
  std::string sourceLine;
  int32_t currentLineIndex = 1;  // The line index is 1-based.

  while (std::getline(sourceStream, sourceLine, '\n')) {
    if (currentLineIndex > lineIndex + extraLineCount) break;
    if (currentLineIndex >= lineIndex - extraLineCount) {
      sourceCode += currentLineIndex == lineIndex ? "===> " : "     ";
      sourceCode += sourceLine;
      sourceCode += "\n";
    }
    ++currentLineIndex;
  }

  return sourceCode;
}

}  // namespace node_lite

int32_t main(int32_t argc, char* argv[]) {
  return node_lite::NodeLiteRuntime::Run(argc, argv);
}
