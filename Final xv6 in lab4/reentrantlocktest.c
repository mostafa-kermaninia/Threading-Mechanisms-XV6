#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  initreentrantlock();

  printf(1, "Acquiring mutex first time...\n");
  acquirereentrant();

  printf(1, "Acquiring mutex second time...\n");
  acquirereentrant();

  printf(1, "Releasing mutex...\n");
  releasereentrant();

  printf(1, "Releasing mutex again...\n");
  releasereentrant();

  printf(1, "Test completed successfully.\n");
  exit();
}
