#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

// zhi 123 -> count

// mkdir test / cd test / ls not working
// zhi path /:bin:asdasd -> set set PATH
// ls works

#define COMMAND_COUNT "count"
#define COMMAND_PARENT "parent"
#define COMMAND_CHILDREN "children"
#define COMMAND_PATH "path"
#define COMMAND_SLEEP "sleep"
#define COMMAND_BARRIER "barrier"
#define COMMAND_REENTRANT "reentrant"

int main(int argc, char *argv[])
{
    printf(1, "----------------------------------\nCurrent pid = %d\n", getpid());
    if(argc < 2){
        printf(1, "Error: Too few args \nuse {count 123}/{parent}/{children 2}/{path /:bin:}\n");
        exit();
    }

    if (strcmp(argv[1], COMMAND_COUNT)  == 0 && argc == 3)
    {
        // We can use ebx or edx 
        int saved_ebx, number = atoi(argv[2]);
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

    if (strcmp(argv[1], COMMAND_PARENT)  == 0 && argc == 2)
    {
        printf(1, "parent id: %d \n", get_parent_id());
        exit();
    }

    if (strcmp(argv[1], COMMAND_PATH)  == 0 && argc == 3)
    {
        printf(1, "adding the pathes to PATH ... \n");
        set_path(argv[2]);
        exit();
    }

    if (strcmp(argv[1], COMMAND_CHILDREN)  == 0 && argc == 3){
        int number = atoi(argv[2]);
        int p1=0, p2=0;
        p1=fork();
        if(p1==0){
            int p3 = fork();
            if(p3!=0)
                printf(1, "new Child of %d: %d\n", getpid(), p3);
            sleep(2);
        }else{
            printf(1, "new Child of %d: %d\n", getpid(), p1);
            p2=fork();
            if(p2!=0)
                printf(1, "new Child of %d: %d\n", getpid(), p2);
            else
            {
                sleep(1);
            }
            
        }

        // if(p1!=0 && p2!=0)
        printf(1, "children of %d are: %d \n", number, get_children(number));
        wait();
        wait();
        wait();
        exit();
    }

    if (strcmp(argv[1], COMMAND_SLEEP)  == 0 && argc == 3)
    {
        int sleep_seconds = atoi(argv[2]);
        printf(1, "user: starting sleep ... \n");
        int t1 = get_time();
        set_sleep(sleep_seconds);
        int t2 = get_time();
        printf(1, "user: sleep real duration: %d\n", t2-t1);
        exit();
    }

    if (strcmp(argv[1], COMMAND_BARRIER)  == 0)
    {
        printf(1, "user: starting barrier ... \n");
        barrier_init(4);
        if(fork() != 0){
            sleep(100);
        }
        fork();
        printf(1, "user: before barrier pid: %d\n", getpid());
        barrier_wait();
        printf(1, "user: after barrier pid: %d\n", getpid());
        wait();
        wait();
        exit();
    }

    if (strcmp(argv[1], COMMAND_REENTRANT)  == 0)
    {
        printf(1, "user: starting reentrant spinlock test ... \n");
        reentrant_spinlock_test();
        printf(1, "user: test completed ... \n");
        exit();
    }

    printf(1, "Error: Bad args \nuse {count 123}/{parent}/{children 2}/{path /:bin:}\n");
        exit();

}
