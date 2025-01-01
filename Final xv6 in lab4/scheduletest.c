#include "types.h"
#include "stat.h"
#include "user.h"

#define NPROC 10

int
fibonacci(int n){
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int
main(void)
{
  int pid, n = 39;

  for(int i = 0; i < NPROC; i++){
    pid = fork();
    if(pid < 0){
      printf(1, "scheduletest: fork failed\n");
      exit();
    }
    if(pid == 0){
      printf(1, "fibonacci(%d) = %d\n", n, fibonacci(n));
      exit();
    }
    if(pid % 3 == 0)
      set_bc(pid, 6, 80);
    if (pid%4 == 0)
      change_queue(pid, 1);
    printf(1, "scheduletest: starting process %d\n", pid);
    processes_info();
  }

  for(int i = 0; i < NPROC; i++){
    printf(1, "scheduletest: ending process %d\n", wait());
    processes_info();
  }

  exit();
}
