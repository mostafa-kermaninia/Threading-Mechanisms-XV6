#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf(2, "Usage: print_syscalls_test <pid>\n");
    exit();
  }

  int pid = atoi(argv[1]);

  printf(1, "Current process id: %d\n", getpid());

  if (sort_syscalls(pid) < 0) {
    printf(2, "Error: Could not print system calls for pid %d\n", pid);
  }

  exit();
}
