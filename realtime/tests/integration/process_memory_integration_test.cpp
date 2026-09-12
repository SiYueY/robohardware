#include <realtime/process_memory.hpp>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  const pid_t child = fork();
  if (child == -1) {
    std::cerr << "fork failed: " << errno << '\n';
    return 1;
  }

  if (child == 0) {
    const auto error = realtime::lock_process_memory();
    if (!error || error.category() == std::system_category()) {
      _exit(EXIT_SUCCESS);
    }
    _exit(EXIT_FAILURE);
  }

  int status{};
  if (waitpid(child, &status, 0) == -1) {
    std::cerr << "waitpid failed: " << errno << '\n';
    return 1;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
    std::cerr << "child process did not preserve lock_process_memory result semantics\n";
    return 1;
  }

  return 0;
}
