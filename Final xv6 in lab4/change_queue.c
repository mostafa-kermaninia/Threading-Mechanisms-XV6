#include "types.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf(2, "Usage: change_queue <pid> <new_q_id>\n");
    exit();
  }

  int pid = atoi(argv[1]);
  int sel_q = atoi(argv[2]);

  change_queue(pid, sel_q);
  exit();
}