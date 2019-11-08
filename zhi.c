#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    if (argc == 2)
    {
        // We can use ebx or edx 
        int saved_ebx, number=atoi(argv[1]);
        // asm("movl %%ebx, %0;" : "=r" (saved_ebx));
        // asm("movl %0, %%ebx;" : : "r"(number) );
        asm volatile(
            "movl %%ebx, %0;"
            "movl %1, %%ebx;"
            : "=r" (saved_ebx)
            : "r"(number)
        );
        // call function by asm
        // asm("movl $22 , %eax;");
        // asm("int $64");
        // count_num_of_digits(atoi(argv[1]));
        printf(1, "count_num_of_digits() from user: \n");
        count_num_of_digits();
        asm("movl %0, %%ebx" : : "r"(saved_ebx) );
        exit();
    }
    else if (argc == 1)
    {
        printf(1, "parent id: %d \n", get_parent_id());
        exit();
    }
    else if (argc == 3)
    {
        printf(1, "adding the pathes to PATH ... \n");
        set_path(argv[2]);
        exit();
    }
}
