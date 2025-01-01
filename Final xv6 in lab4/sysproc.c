#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int sys_fork(void)
{
  return fork();
}

int sys_exit(void)
{
  exit();
  return 0; // not reached
}

int sys_wait(void)
{
  return wait();
}

int sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int sys_getpid(void)
{
  return myproc()->pid;
}

int sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

int sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_create_palindrome(void)
{
  int num;
  struct proc *p = myproc();
  num = p->tf->ebx;
  create_palindrome(num);
  return 0;
}

int sys_sort_syscalls(void) {
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return sort_syscalls(pid);
}

int sys_get_most_invoked_syscall(){
  int pid;
  if (argint(0, &pid) < 0)
    return -1;
  return get_most_invoked_syscall(pid);
}

int sys_list_all_processes(void){
  return list_all_processes();
}

int sys_change_queue(void){
  int pid, sel_q;
  if (argint(0, &pid) < 0)
    return -1;
  if (argint(1, &sel_q) < 0)
    return -1;
  change_queue(pid, sel_q);
  return 0;
}

int sys_processes_info(void){
  processes_info();
  return 0;
}

int sys_set_bc(void){
  int pid, bursttime, confidence;
  if (argint(0, &pid) < 0)
    return -1;
  if (argint(1, &bursttime) < 0)
    return -1;
  if (argint(2, &confidence) < 0)
    return -1;
  set_bc(pid, bursttime, confidence);
  return 0;
}