#include "types.h"
#include "stat.h"
#include "user.h"


int main(int argc, char *argv[])
{
  int child_count = atoi(argv[1]);
  if(child_count > 100) {
    printf(1, "child count should be less than 100\n");
    exit();
  }
  int for_duration = atoi(argv[2]);
  int level = atoi(argv[3]);
  int is_child = 0;
  for(int i = 0; i < child_count; i++) {
    int pid = fork();
    if(pid == 0) {
      is_child = 1;
      break;
    }
    else if (pid > 0) {
      change_process_level(pid, level);
    }
    else {
      printf(1, "error forking\n");
    }
  }
  if (is_child == 0) {
    print_processes_info();
  }
  int c = 2;
  for(int i = 0; i < for_duration; i++)
    c *= 2;
  if (is_child == 0) {
    int pid = getpid();
    for(int i = 0; i < child_count; i++) {
      wait();
    }
    change_process_level(pid, level);
  }

  printf(1, "%d", c);
  exit();
}
