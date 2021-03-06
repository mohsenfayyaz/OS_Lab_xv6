#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "date.h"

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
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

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
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
  p->level = 0;
  uint now;
  acquire(&tickslock);
  now = ticks;
  release(&tickslock);
  p->arrTime = now;
  p->ticket = 10;
  p->cycleNum = 1;
  p->remaining_priority = 10;
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

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
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
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}

int pow(int a, int n)
{
  if (n == 0)
    return 1;
  int p = a;
  for (int i = 1; i < n; i++)
  {
    p = p * a;
  }
  return p;
}

int get_children_of(int pid)
{
  // int children[100];
  int concat_children = 0;
  int num_of_children = 0;
  int has_zero = 0;
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent->pid == pid)
    {
      if (p->pid == 0)
      {
        has_zero = 1;
      }
      // children[num_of_children] = p->pid;
      concat_children += p->pid * pow(10, num_of_children);
      num_of_children++;
    }
  }
  release(&ptable.lock);

  // children[num_of_children] = -1;
  if (concat_children == 0 && has_zero == 0)
    concat_children = -1;
  return concat_children;
}

void run_p(struct cpu *c, struct proc *p)
{
  p->cycleNum++;
  if (p->state != RUNNABLE)
    return;

  // p->cycleNum++;
  // Switch to chosen process.  It is the process's job
  // to release ptable.lock and then reacquire it
  // before jumping back to us.
  c->proc = p;
  switchuvm(p);
  p->state = RUNNING;

  swtch(&(c->scheduler), p->context);
  switchkvm();

  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;
}

void run_third_level_processes()
{
  struct proc *p;
  struct cpu *c = mycpu();
  int min_priority = 1000000000;
  int min_priority_pid = -1;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->level == 2 && p->state == RUNNABLE)
    {
      if (p->remaining_priority < min_priority)
      {
        min_priority = p->remaining_priority;
        min_priority_pid = p->pid;
      }
    }
  }
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == min_priority_pid)
    {
      if (p->remaining_priority - 1 >= 0)
      {
        p->remaining_priority -= 1;
      }
      run_p(c, p);
    }
  }
}

void run_second_level_processes()
{
  struct proc *p;
  struct cpu *c = mycpu();
  uint now;

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);
  double max_hrrn = -1;
  double curr_hrrn = 0;
  int max_hrrn_pid = -1;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->level == 1 && p->state == RUNNABLE)
    {
      double waiting_time = now - p->arrTime;
      curr_hrrn = waiting_time / p->cycleNum;
      if (curr_hrrn > max_hrrn)
      {
        max_hrrn = curr_hrrn;
        max_hrrn_pid = p->pid;
      }
    }
  }
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == max_hrrn_pid)
    {
      // p->cycleNum++;
      run_p(c, p);
    }
  }
}

void run_first_level_processes()
{
  struct proc *p;
  struct cpu *c = mycpu();
  int random_ticket = 0;
  int random_counter = 0;
  int max_ticket_number = 0;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->level == 0)
    {
      max_ticket_number += p->ticket;
    }
  }
  uint rand;
  acquire(&tickslock);
  rand = ticks;
  release(&tickslock);
  random_ticket = (rand * 11 + 13) % max_ticket_number + 1;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->level == 0)
    {
      random_counter += p->ticket;
      if (random_ticket <= random_counter)
      {
        run_p(c, p);
      }
    }
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    int ran = 0;
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->level == 0 && p->state == RUNNABLE)
      {
        ran = 1;
        run_first_level_processes();
      }
    }
    if(ran) {
      release(&ptable.lock);
      continue;
    }
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->level == 1 && p->state == RUNNABLE)
      {
        ran = 1;
        run_second_level_processes();
      }
    }
    if(ran) {
      release(&ptable.lock);
      continue;
    }
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->level == 2 && p->state == RUNNABLE)
      {
        run_third_level_processes();
      }
    }
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
  acquire(&ptable.lock); //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
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
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
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
  { //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
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

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  // static char *states[] = {
  //     [UNUSED] "unused",
  //     [EMBRYO] "embryo",
  //     [SLEEPING] "sleep ",
  //     [RUNNABLE] "runble",
  //     [RUNNING] "run   ",
  //     [ZOMBIE] "zombie"};
  // int i;
  // struct proc *p;
  // char *state;
  // uint pc[10];

  // for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  // {
  //   if (p->state == UNUSED)
  //     continue;
  //   if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
  //     state = states[p->state];
  //   else
  //     state = "???";
  //   cprintf("%d %s %s", p->pid, state, p->name);
  //   if (p->state == SLEEPING)
  //   {
  //     getcallerpcs((uint *)p->context->ebp + 2, pc);
  //     for (i = 0; i < 10 && pc[i] != 0; i++)
  //       cprintf(" %p", pc[i]);
  //   }
  //   cprintf("\n");
  // }
  print_processes_info();
}

void change_process_level(int pid, int level)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->level = level;
    }
  }
}
void set_process_ticket(int pid, int ticket)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->ticket = ticket;
    }
  }
}
void set_process_remaining_priority(int pid, int priority)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->remaining_priority = priority;
    }
  }
}

void reverse(char* str, int len) 
{ 
    int i = 0, j = len - 1, temp; 
    while (i < j) { 
        temp = str[i]; 
        str[i] = str[j]; 
        str[j] = temp; 
        i++; 
        j--; 
    } 
} 
// Converts a given integer x to string str[].  
// d is the number of digits required in the output.  
// If d is more than the number of digits in x,  
// then 0s are added at the beginning. 
int intToStr(int x, char* str, int d) 
{ 
    int i = 0; 
    if(x == 0) {
      str[i++] = '0';
    }
    while (x) { 
        str[i++] = (x % 10) + '0'; 
        x = x / 10; 
    } 
  
    // If number of digits required is more, then 
    // add 0s at the beginning 
    while (i < d) 
        str[i++] = '0'; 
  
    reverse(str, i); 
    str[i] = '\0'; 
    return i; 
} 
  
// Converts a floating-point/double number to a string. 
void ftoa(float n, char* res, int afterpoint) 
{ 
    // Extract integer part 
    int ipart = (int)n; 
  
    // Extract floating part 
    float fpart = n - (float)ipart; 
  
    // convert integer part to string 
    int i = intToStr(ipart, res, 0); 
  
    // check for display option after point 
    int pwr = 1;
    for(int j = 0; j < afterpoint; j++) {
      pwr *= 10;
    }
    res[i] = '.'; // add dot 
    fpart = fpart * pwr; 
  
    intToStr((int)fpart, res + i + 1, afterpoint); 
} 

void print_processes_info()
{
  uint now;
  static char *states[] = {
    [UNUSED]    " UNUSED ",
    [EMBRYO]    " EMBRYO ",
    [SLEEPING]  "SLEEPING",
    [RUNNABLE]  "RUNNABLE",
    [RUNNING]   "RUNNING ",
    [ZOMBIE]    " ZOMBIE "
  };
  acquire(&tickslock);
  now = ticks;
  release(&tickslock);
  struct proc *p;
  double curr_hrrn = 0;
  static char hrrn_str[30], priority_str[30], cycle_str[30];
  cprintf("Name        PID        State        Level        Tickets        CycleNum        HRRN        RemainingPriority\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == UNUSED) {
      continue;
    }
    double waiting_time = now - p->arrTime;
    curr_hrrn = waiting_time / p->cycleNum;
    ftoa(curr_hrrn, hrrn_str, 2);
    ftoa((float)p->cycleNum, cycle_str, 2);
    ftoa((float)p->remaining_priority/10, priority_str, 1);
    cprintf("%s        %d        %s        %d        %d",
            p->name, p->pid, states[p->state], p->level, p->ticket);
    cprintf("        %s", cycle_str);
    cprintf("        %s        %s \n", hrrn_str, priority_str);
  }
}

//  Lab 04

struct
{
  int max_count;
  int counter;
} barrier;

void barrier_init(int barrier_count)
{
  barrier.counter = 0;
  barrier.max_count = barrier_count;
  cprintf("Kernel: barrier initialized.\n");
  return ;
}

void barrier_wait(){
  barrier.counter++;
  // cprintf("%d, %d\n", barrier.counter, barrier.max_count);
  if(barrier.counter < barrier.max_count){
    acquire(&ptable.lock);
    sleep(/*channel*/ &barrier, &ptable.lock);
    release(&ptable.lock);
  }else{
    cprintf("-------------------\nKernel: Barrier is broken!\n");
    wakeup(&barrier);
  }
}

void reentrant_spinlock_test(){
  cprintf("Kernel: spinlock init!\n");
  struct spinlock lock;
  initlock(&lock, "splinlock");
  acquire_reentrant(&lock);
  acquire_reentrant(&lock);
  // acquire(&lock);
  // acquire(&lock);
  release(&lock);
  cprintf("Kernel: spinlock release!\n");
}