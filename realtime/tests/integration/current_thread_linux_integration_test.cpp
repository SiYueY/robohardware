#include <realtime/current_thread.hpp>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

int child_main() {
  cpu_set_t allowed{};
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
    return EXIT_FAILURE;
  }

  unsigned int selected_cpu = CPU_SETSIZE;
  for (unsigned int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(static_cast<int>(cpu), &allowed)) {
      selected_cpu = cpu;
      break;
    }
  }
  if (selected_cpu == CPU_SETSIZE) {
    return EXIT_FAILURE;
  }

  const auto error =
      realtime::set_current_thread_affinity(selected_cpu);
  if (error) {
    return EXIT_FAILURE;
  }

  cpu_set_t observed{};
  if (sched_getaffinity(0, sizeof(observed), &observed) != 0) {
    return EXIT_FAILURE;
  }
  return CPU_COUNT(&observed) == 1 && CPU_ISSET(static_cast<int>(selected_cpu), &observed)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

}  // namespace

int main() {
  const pid_t child = fork();
  if (child == -1) {
    std::cerr << "fork failed: " << errno << '\n';
    return 1;
  }
  if (child == 0) {
    _exit(child_main());
  }

  int status{};
  if (waitpid(child, &status, 0) == -1) {
    std::cerr << "waitpid failed: " << errno << '\n';
    return 1;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
    std::cerr << "set_current_thread_affinity did not produce a singleton affinity mask\n";
    return 1;
  }
  return 0;
}
