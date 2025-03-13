// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "../../API/hermes_node_api/hermes_node_api.h"
#include "../../include/hermes/VM/Runtime.h"
#include "node_lite.h"

namespace node_lite {

class HermesNodeLiteAdapter : public INodeLiteRuntimeAdapter {
 public:
  HermesNodeLiteAdapter()
      : runtime_(
            hermes::vm::Runtime::create(hermes::vm::RuntimeConfig::Builder()
                                            .withMicrotaskQueue(true)
                                            .build())) {}

  napi_env GetEnv() override {
    hermes::vm::CallResult<napi_env> env_result =
        hermes::node_api::getOrCreateRuntimeNodeApiEnvironment(*runtime_,
                                                               NAPI_VERSION);
    if (env_result.getStatus() == hermes::vm::ExecutionStatus::EXCEPTION) {
      throwPendingError();
    }
    return env_result.getValue();
  }

  // TODO: Should it be removed?
  napi_env CreateModuleEnv(int32_t api_version) override {
    hermes::vm::CallResult<napi_env> env_result =
        hermes::node_api::createModuleNodeApiEnvironment(*runtime_,
                                                               NAPI_VERSION);
    if (env_result.getStatus() == hermes::vm::ExecutionStatus::EXCEPTION) {
      throwPendingError();
    }
    return env_result.getValue();
  }

  [[noreturn]] void throwPendingError() {
    hermes::vm::GCScope scope{*runtime_};

    // Retrieve the exception value and clear as we will rethrow it as a C++
    // exception.
    auto hv = runtime_->getThrownValue();
    runtime_->clearThrownValue();
    //auto jsiVal = valueFromHermesValue(hv);
    //auto hnd = vmHandleFromValue(jsiVal);

    std::string msg = "No message";
    std::string stack = "No stack";
    //if (auto str = vm::Handle<vm::StringPrimitive>::dyn_vmcast(hnd)) {
    //  // If the exception is a string, use it as the message.
    //  msg = utf8FromStringView(
    //      vm::StringPrimitive::createStringView(runtime_, str));
    //} else if (auto obj = vm::Handle<vm::JSObject>::dyn_vmcast(hnd)) {
    //  // If the exception is an object try to retrieve its message and stack
    //  // properties.

    //  /// Attempt to retrieve a string property \p sym from \c obj and store it
    //  /// in \p out. Ignore any catchable errors and non-string properties.
    //  auto getStrProp = [this, obj](vm::SymbolID sym, std::string& out) {
    //    auto propRes = vm::JSObject::getNamed_RJS(obj, runtime_, sym);
    //    if (LLVM_UNLIKELY(propRes == vm::ExecutionStatus::EXCEPTION)) {
    //      // An exception was thrown while retrieving the property, if it is
    //      // catchable, suppress it. Otherwise, rethrow this exception without
    //      // trying to invoke any more JavaScript.
    //      auto propExHv = runtime_.getThrownValue();
    //      runtime_.clearThrownValue();

    //      if (!vm::isUncatchableError(propExHv)) return;

    //      // An uncatchable error occurred, it is unsafe to do anything that
    //      // might execute more JavaScript.
    //      throw jsi::JSError(
    //          valueFromHermesValue(propExHv),
    //          "Uncatchable exception thrown while creating error",
    //          "No stack");
    //    }

    //    // If the property is a string, update out. Otherwise ignore it.
    //    auto prop = propRes->get();
    //    if (prop.isString()) {
    //      auto view = vm::StringPrimitive::createStringView(
    //          runtime_, runtime_.makeHandle(prop.getString()));
    //      out = utf8FromStringView(view);
    //    }
    //  };

    //  getStrProp(vm::Predefined::getSymbolID(vm::Predefined::message), msg);
    //  getStrProp(vm::Predefined::getSymbolID(vm::Predefined::stack), stack);
    //}

    // Use the constructor of jsi::JSError that cannot run additional
    // JS, since that may then result in additional exceptions and infinite
    // recursion.
    throw NodeLiteException(msg, stack);
  }

 private:
  std::shared_ptr<hermes::vm::Runtime> runtime_;
};

}  // namespace node_lite

int32_t main(int32_t argc, char* argv[]) {
  return node_lite::NodeLiteRuntime::Run(
      argc, argv, std::make_unique<node_lite::HermesNodeLiteAdapter>());
}
