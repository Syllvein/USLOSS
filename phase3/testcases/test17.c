/* basic terminate test */

#include <usyscall.h>
#include <libuser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>


int Child1(char *);
int Child2(char *);

int sem1;

int start3(char *arg)
{
    int pid;
    int status;

    printf("start3(): started\n");
    Spawn("Child1", Child1, "Child1", USLOSS_MIN_STACK, 4, &pid);
    printf("start3(): spawned process %d\n", pid);
    Wait(&pid, &status);
    printf("start3(): child %d returned status of %d\n", pid, status);
    printf("start3(): done\n");
    Terminate(8);
    return 0;
} /* start3 */


int Child1(char *arg) 
{
    int pid, status;

    console("Child1() starting\n");
    Spawn("Child2", Child2, "Child2", USLOSS_MIN_STACK, 5, &pid);
    printf("Child1(): spawned process %d\n", pid);
    Wait(&pid, &status);
    printf("Child1(): child %d returned status of %d\n", pid, status);
    Spawn("Child3", Child2, "Child3", USLOSS_MIN_STACK, 5, &pid);
    printf("Child1(): spawned process %d\n", pid);
    Wait(&pid, &status);
    printf("Child1(): child %d returned status of %d\n", pid, status);

    console("Child1(): done\n");
    Terminate(9);
    return 0;
} /* Child1 */


int Child2(char *arg) 
{

    if(!(strcmp(arg,"Child2"))){
        console("Child2(): starting\n");
        Terminate(9);
    }
    else{
        if(!(strcmp(arg,"Child3"))){
            console("Child3(): starting\n");
            Terminate(10);
        }
        else{
            console("wrong argument passed ... test failed\n");
            exit(1);
        }
    }
    return 0;
} /* Child2 */
