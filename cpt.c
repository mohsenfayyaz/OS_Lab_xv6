#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char buf[512];

void cpt(int in, int out)
{
  int n;

  while ((n = read(in, buf, sizeof(buf))) > 0)
  {
    if (write(out, buf, n) != n)
    {
      printf(1, "cpt: write error\n");
      exit();
    }
    if (buf[n - 1] == '\n')
    {
      exit();
    }
  }
  if (n < 0)
  {
    printf(1, "cpt: read error\n");
    exit();
  }
}

int main(int argc, char *argv[])
{
  int out, in;

  if (argc <= 1)
  {
    printf(1, "cpt: too few arguments\n");
    exit();
  }
  else if (argc == 2)
  {
    if ((out = open(argv[1], O_CREATE | O_RDWR)) < 0)
    {
      printf(1, "cpt: cannot open %s\n", argv[1]);
      exit();
    }
    cpt(0, out);
    close(out);
  }
  else if (argc == 3)
  {
    if ((in = open(argv[1], 0)) < 0)
    {
      printf(1, "cpt: cannot open %s\n", argv[1]);
      exit();
    }
    if ((out = open(argv[2], O_CREATE | O_RDWR)) < 0)
    {
      printf(1, "cpt: cannot create or edit %s\n", argv[2]);
      close(in);
      exit();
    }
    cpt(in, out);
    close(in);
    close(out);
  }
  else
  {
    printf(1, "cpt: too many arguments\n");
  }
  exit();
}
