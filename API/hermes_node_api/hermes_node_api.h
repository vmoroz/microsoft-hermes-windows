/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Copyright (c) Microsoft Corporation.
 * Licensed under the MIT license.
 */

#ifndef HERMES_NODE_API_H
#define HERMES_NODE_API_H

#include "hermes/VM/Runtime.h"
#include "js_native_api.h"

namespace hermes::node_api {

napi_status GetOrCreateRuntimeNodeApiEnvironment(
    vm::Runtime &runtime,
    int32_t apiVersion,
    napi_env *env) noexcept;

napi_status CreateModuleNodeApiEnvironment(
    napi_env rootEnvironment,
    int32_t apiVersion,
    napi_env *env) noexcept;

} // namespace hermes::node_api

#endif // HERMES_NODE_API_H
