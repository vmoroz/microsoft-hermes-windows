#include "child_process.h"

#include <cassert>
#include <cstdio>
#include "string_utils.h"

#ifndef VerifyElseExit
#define VerifyElseExit(condition)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      ExitOnError(#condition);                                                 \
    }                                                                          \
  } while (false)
#endif

namespace node_api_tests {

// Create a child process that uses the previously created pipes for STDIN and
// STDOUT.
ProcessResult SpawnSync(std::string_view command,
                        std::vector<std::string> args) {
  ProcessResult result{};

  return result;
}

}  // namespace node_api_tests