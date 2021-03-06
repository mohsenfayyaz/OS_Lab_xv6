#include "types.h"
#include "stat.h"
#include "user.h"


int main(int argc, char *argv[])
{
  int child_count = argc == 4 ? atoi(argv[1]) : 10;
  if(child_count > 100) {
    printf(1, "child count should be less than 100\n");
    exit();
  }
  int for_duration = argc == 4 ? atoi(argv[2]) : 1000000000;
  int level = argc == 4 ? atoi(argv[3]) : 0;
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
  int c = 2;
  for(int i = 0; i < for_duration; i++)
    c *= 2;
  if (is_child == 0) {
    for(int i = 0; i < child_count; i++) {
      wait();
    }
    int pid = getpid();
    change_process_level(pid, level);
  }

  printf(1, "%d", c);
  exit();
}
