#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        write(1, "not enough arguments\n", 22);
        exit();
    }
    printf(1, "number of digits:  %d \n", count_num_of_digits(atoi(argv[1])));
    exit();
}
