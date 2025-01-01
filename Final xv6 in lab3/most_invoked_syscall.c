#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf(2, "Usage: most_invoked_syscall <pid>\n");
    exit();
  }

  int pid = atoi(argv[1]);

  if (get_most_invoked_syscall(pid) < 0) {
    printf(2, "Error: Could not print most invoked system call for pid %d\n", pid);
  }

  exit();
}