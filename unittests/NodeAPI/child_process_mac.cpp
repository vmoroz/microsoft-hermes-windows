#include "child_process.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifndef VerifyElseExit
#define VerifyElseExit(condition)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      ExitOnError(#condition);                                                 \
    }                                                                          \
  } while (false)
#endif

extern char** environ;

namespace node_api_tests {

namespace {

std::string ReadFromFd(int fd);
void ExitOnError(const char* message);

}  // namespace

ProcessResult SpawnSync(std::string_view command,
                        std::vector<std::string> args) {
  ProcessResult result{};

  // These int arrays each comprise two file descriptors: { readEnd, writeEnd }.
  int stdout_pipe[2], stderr_pipe[2];
  VerifyElseExit(pipe(stdout_pipe) == 0);
  VerifyElseExit(pipe(stderr_pipe) == 0);

  posix_spawn_file_actions_t actions;
  VerifyElseExit(posix_spawn_file_actions_init(&actions) == 0);

  VerifyElseExit(posix_spawn_file_actions_adddup2(
                     &actions, stdout_pipe[1], STDOUT_FILENO) == 0);
  VerifyElseExit(posix_spawn_file_actions_adddup2(
                     &actions, stderr_pipe[1], STDERR_FILENO) == 0);

  VerifyElseExit(posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]) ==
                 0);
  VerifyElseExit(posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]) ==
                 0);

  std::vector<char*> argv;
  argv.push_back(strdup(std::string(command).c_str()));
  for (const std::string& arg : args) {
    argv.push_back(strdup(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid;
  VerifyElseExit(
      posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) ==
      0);

  posix_spawn_file_actions_destroy(&actions);

  // Close the write ends of the pipes.
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  int wait_status;
  VerifyElseExit(waitpid(pid, &wait_status, 0) == pid);

  result.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 1;
  result.std_output = ReadFromFd(stdout_pipe[0]);
  result.std_error = ReadFromFd(stderr_pipe[0]);

  // Close the read ends of the pipes.
  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  for (char* arg : argv) {
    free(arg);
  }

  return result;
}

namespace {

std::string ReadFromFd(int fd) {
  std::string result;
  constexpr size_t bufferSize = 4096;
  char buffer[bufferSize];
  ssize_t bytesRead;
  while ((bytesRead = read(fd, buffer, bufferSize)) > 0) {
    result.append(buffer, bytesRead);
  }
  return result;
}

// Format a readable error message, print it to console, and exit from the
// application.
void ExitOnError(const char* message) {
  int err = errno;
  const char* err_msg = strerror(err);

  fprintf(stderr, "%s failed with error %d: %s\n", message, err, err_msg);

  exit(1);
}

}  // namespace
}  // namespace node_api_tests
