// Reentranting locks

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "reentrantlock.h"

void
initreentrantlock(struct reentrantlock *lk)
{
  initlock(&lk->lock, "reentrant lock");
  lk->owner = 0;
  lk->recursion = 0;
}

void
acquirereentrant(struct reentrantlock *lk)
{
  pushcli(); // Disable interrupts to avoid deadlock.

  // Check for the new process to hold the lock
  if (lk->owner != myproc()) {
    // Acquire the underlying spinlock
    acquire(&lk->lock);
    if (lk->recursion != 0)
      panic("reentrant lock: acquire");
    lk->owner = myproc();
  }

  lk->recursion++;
  popcli();
}

void
releasereentrant(struct reentrantlock *lk)
{
  pushcli();

  if (lk->owner != myproc())
    panic("reentrant lock: release");

  lk->recursion--;

  // Decrement the lock count or release the spinlock
  if (lk->recursion == 0) {
    lk->owner = 0;
    release(&lk->lock);
  }

  popcli();
}