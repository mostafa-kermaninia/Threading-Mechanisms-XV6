#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf(2, "Usage: move_test <source_file> <destination_dir>\n");
    exit();
  }

  if (move_file(argv[1], argv[2]) < 0) {
    printf(2, "move_file failed\n");
  } else {
    printf(1, "File moved successfully\n");
  }

  exit();
}
