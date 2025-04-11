// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef NODE_API_TEST_STRING_UTILS_H
#define NODE_API_TEST_STRING_UTILS_H

#include <string>
#include <string_view>
#include <vector>

namespace node_api_tests {

extern std::string FormatString(const char* format, ...) noexcept;

extern std::string ReplaceAll(std::string str,
                              std::string_view from,
                              std::string_view to) noexcept;

}  // namespace node_api_tests

#endif  // !NODE_API_TEST_STRING_UTILS_H