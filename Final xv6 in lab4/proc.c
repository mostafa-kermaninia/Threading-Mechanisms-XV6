#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nextfcfs = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // Initialize number of each system call
  memset(p->syscall_invokes, 0, sizeof(p->syscall_invokes));

  // Initialize number of system calls
  p->syscalls_count = 0;

  // Default scheduling queue except init and shell
  if (p->pid == 1 ||
      p->pid == 2)
    p->schedqueue = RR;
  else
  {
    p->schedqueue = FCFS;
    p->fcfsentry = nextfcfs++;
  }
  // Initialize arraival time and wait time
  p->arraival = ticks;
  p->wait_time = 0;

  // Initialize consecutive_time
  p->consecutive_time = 0;

  // Default SJF values
  p->bursttime = 2;
  p->confidence = 50;

  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  // Change queue for shell's forks
  if (curproc->pid == 2)
    np->schedqueue = RR;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

unsigned long randstate = 42;
unsigned int
rand()
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}

// Switch to chosen process.  It is the process's job
// to release ptable.lock and then reacquire it
// before jumping back to us.
void switch_to_chosen_process(struct proc *p, struct cpu *c)
{
  c->proc = p;
  switchuvm(p);
  p->state = RUNNING;
  p->wait_time = 0;

  swtch(&(c->scheduler), p->context);
  switchkvm();

  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p, *nextp, *nextrrp = ptable.proc, *longestjob;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    nextp = 0;
    acquire(&ptable.lock);
    switch (c->schedqueue)
    {
    case RR:
      // Loop over process table looking for process to run.
      p = nextrrp;
      do
      {
        if (p->state != RUNNABLE || p->schedqueue != RR)
        {
          if (++p == &ptable.proc[NPROC])
            p = ptable.proc;
          continue;
        }

        nextp = p;

        if (++p == &ptable.proc[NPROC])
          p = ptable.proc;
        nextrrp = p;
        break;
      } while (p != nextrrp);
      break;

    case SJF:
      longestjob = 0;
      // Loop over process table looking for the shortest process to run.
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->state != RUNNABLE || p->schedqueue != SJF)
          continue;

        if (nextp == 0 || p->bursttime < nextp->bursttime)
          if (rand() % 100 < p->confidence)
            nextp = p;

        if (longestjob == 0 || p->bursttime > longestjob->bursttime)
          longestjob = p;
      }
      if (nextp == 0)
        nextp = longestjob;
      break;

    case FCFS:
      // Loop over process table looking for the first process to run.
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->state != RUNNABLE || p->schedqueue != FCFS)
          continue;

        if (nextp == 0 || p->fcfsentry < nextp->fcfsentry)
          nextp = p;
      }
      break;
    }

    // start next queue if queue is empty
    if (nextp == 0)
    {
      c->schedqueue = (c->schedqueue + 1) % NSCHEDQUEUE;
      c->queueticks = 0;
    }
    else
      switch_to_chosen_process(nextp, c);

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

void age_processes(void)
{
  acquire(&ptable.lock);
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != RUNNABLE)
      continue;
    p->wait_time++;
    if (p->wait_time == 800)
    {
      switch (p->schedqueue)
      {
      case FCFS:
        p->wait_time = 0;
        p->schedqueue = SJF;
        p->arraival = ticks;
        cprintf("pid:%d perv_queue:FCFS new_queue:SJF\n", p->pid);
        break;
      case SJF:
        p->wait_time = 0;
        p->schedqueue = RR;
        p->arraival = ticks;
        cprintf("pid:%d perv_queue:SJF new_queue:RR\n", p->pid);
        break;
      default:
        break;
      }
    }
  }
  release(&ptable.lock);
}

void add_consecutive(){
  
  acquire(&ptable.lock);
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == RUNNING)
      p->consecutive_time++;
    else 
      p->consecutive_time = 0;
  }
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void create_palindrome(int num)
{
  int ans = num;
  while (num != 0)
  {
    ans = ans * 10 + num % 10;
    num /= 10;
  }
  cprintf("%d\n", ans);
}

int sort_syscalls(int pid)
{
  int i, j, tmp_num, last_one = 0;
  char *tmp_name;
  struct proc *p;

  // Find the process with the given PID
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      // Sort system calls
      for (i = 0; i < p->syscalls_count - 1; i++)
      {
        for (j = 0; j < p->syscalls_count - i - 1; j++)
        {
          if (p->syscall_num[j] > p->syscall_num[j + 1])
          {
            tmp_num = p->syscall_num[j];
            tmp_name = p->syscall_name[j];

            p->syscall_num[j] = p->syscall_num[j + 1];
            p->syscall_name[j] = p->syscall_name[j + 1];

            p->syscall_num[j + 1] = tmp_num;
            p->syscall_name[j + 1] = tmp_name;
          }
        }
      }

      cprintf("System calls for process %d:\n", pid);
      for (i = 0; i < p->syscalls_count - 1; i++)
        if (p->syscall_num[i] != last_one)
        {
          cprintf("Syscall #%d: %s\n", p->syscall_num[i], p->syscall_name[i]);
          last_one = p->syscall_num[i];
        }

      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int get_most_invoked_syscall(int pid)
{
  char *syscall_name = "";
  int syscall_invokes = 0;
  int syscall_num = 0;
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      for (int i = 0; i < p->syscalls_count; i++)
      {
        if (syscall_invokes <= p->syscall_invokes[p->syscall_num[i]])
        {

          syscall_invokes = p->syscall_invokes[p->syscall_num[i]];
          syscall_name = p->syscall_name[i];
          syscall_num = p->syscall_num[i];
        }
      }
      if (syscall_invokes > 0)
      {
        cprintf("Most invoked syscall for process %d is %s with %d invokes\n", p->pid, syscall_name,
                syscall_invokes);
        release(&ptable.lock);
        return syscall_num;
      }
    }
  }
  release(&ptable.lock);
  return -1;
}

int list_all_processes()
{
  int p_count = 0;
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == RUNNING)
    {
      p_count++;
      cprintf("Process %d with %d Syscalls\n", p->pid, p->syscalls_count);
    }
  }
  release(&ptable.lock);
  return (p_count == 0) ? -1 : 0;
}

void change_queue(int pid, int chosen_q)
{
  if (chosen_q < 0 || chosen_q > 2)
  {
    cprintf("Invalid queue\n");
    return;
  }

  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid && chosen_q != p->schedqueue)
    {
      cprintf("pid: %d perv_q:%d new_q:%d\n", pid, p->schedqueue, chosen_q);
      p->arraival = ticks;
      p->schedqueue = chosen_q;
      if (chosen_q == FCFS)
        p->fcfsentry = nextfcfs++;
    }
  }
  release(&ptable.lock);
}

void processes_info(void)
{
  struct proc *p;

  acquire(&ptable.lock);
  cprintf(".....................................\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != UNUSED)
    {
      char *state_name;
      switch (p->state)
      {
      case EMBRYO:
        state_name = "EMBRYO";
        break;
      case SLEEPING:
        state_name = "SLEEPING";
        break;
      case RUNNABLE:
        state_name = "RUNNABLE";
        break;
      case RUNNING:
        state_name = "RUNNING";
        break;
      case ZOMBIE:
        state_name = "ZOMBIE";
        break;
      default:
        state_name = "UNKNOWN";
        break;
      }

      cprintf("name:%s pid:%d state:%s queue:%d wait:%d confidence:%d burst time:%d consecutive:%d arrival:%d\n"
              , p->name, p->pid, state_name, p->schedqueue,
              p->wait_time, p->confidence, p->bursttime, p->consecutive_time, p->arraival);
    }
  }
  cprintf(".....................................\n");
  release(&ptable.lock);
}


void set_bc(int pid, int bursttime, int confidence)
{
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->bursttime = bursttime;
      p->confidence = confidence;
    }
  }
  release(&ptable.lock);
}