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

int sys_count_num_of_digits(void)
{
  // int number = myproc()->tf->edi;
  int number = 0;
  int num_of_digits = 0;
  // if (argint(0, &number) < 0)
  //   return -1;
  number = myproc()->tf->ebx; //number = ebx
  int save_number = number;
  while (number != 0)
  {
    num_of_digits++;
    number /= 10;
  }
  cprintf("From Kernel: #digits of %d is %d\n", save_number, num_of_digits);
  // return num_of_digits;
  return 0;
}

int sys_get_parent_id(void)
{
  return myproc()->parent->pid;
}

int pow2(int a, int n)
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

int sys_get_children(void)
{
  int pid;
  if (argint(0, &pid) < 0)
    return -1;
  // int children = get_children_of(pid);
  int grandchildren = 0;
  int grand_counter = 0;
  int exploring_queue = pid;
  while (exploring_queue > 0 || pid == 0)
  {
    if (pid == 0)
      pid = -1;
    int children = get_children_of(exploring_queue % 10);
    // cprintf("%d|%d|%d\n", exploring_queue, exploring_queue%10, get_children_of(exploring_queue%10));
    exploring_queue /= 10;

    int temp_children = children;
    while (temp_children > 0)
    {
      int new_child_id = temp_children % 10;
      grandchildren += new_child_id * pow2(10, grand_counter);
      temp_children /= 10;
      grand_counter++;

      exploring_queue = exploring_queue * 10 + new_child_id;
    }
  }
  // cprintf("|%d|%d|\n", pid, children);
  // return children;
  if (grandchildren == 0)
  {
    return -1;
  }
  return grandchildren;
  // TODO have to pass the children pid in some way
}

int sys_set_path(void)
{
  char *arg;
  if (argstr(0, &arg) < 0)
    return -1;
  // cprintf(arg);
  add_path(arg);
  return 0;
}

int sys_set_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n * 90)
  {
    release(&tickslock);
    sti();
    acquire(&tickslock);
  }
  release(&tickslock);

  return 0;
}

int sys_get_time(void)
{
  struct rtcdate *t;
  if (argptr(0, (void *)&t, sizeof(&t)) < 0)
    return -1;
  cmostime(t);
  int second = t->second + t->minute * 60 + t->hour * 3600;
  return second;
}
int sys_change_process_level(void)
{
  int pid, level;
  if (argint(0, &pid) < 0)
    return -1;
  if (argint(1, &level) < 0)
    return -1;
  change_process_level(pid, level);
  return 0;
}
int sys_set_process_ticket(void)
{
  int pid, ticket;
  if (argint(0, &pid) < 0)
    return -1;
  if (argint(1, &ticket) < 0)
    return -1;
  set_process_ticket(pid, ticket);
  return 0;
}
int sys_set_process_remaining_priority(void)
{
  int pid, remaining_priority;
  if (argint(0, &pid) < 0)
    return -1;
  if (argint(1, &remaining_priority))
    return -1;
  set_process_remaining_priority(pid, remaining_priority);
  return 0;
}
int sys_print_processes_info(void)
{
  print_processes_info();
  return 0;
}