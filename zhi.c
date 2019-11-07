#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    if (argc == 2)
    {
        printf(1, "number of digits:  %d \n", count_num_of_digits(atoi(argv[1])));
        exit();
    }
    else if (argc == 1)
    {
        printf(1, "parent id: %d \n", get_parent_id());
        exit();
    }
}
