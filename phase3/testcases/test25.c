/* recursive terminate test & child cleanup*/



#include <usyscall.h>
#include <libuser.h>
#include <stdio.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

int Child1(char *);
int Child2(char *);
int Child2a(char *);
int Child2b(char *);
int Child2c(char *);

int start3(char *arg)
{
   int pid;
   int status;

   console("start3(): started\n");
   Spawn("Child1", Child1, "Child1", USLOSS_MIN_STACK, 4, &pid);
   console("start3(): spawned process %d\n", pid);
   Wait(&pid, &status);
   console("start3(): child %d returned status of %d\n", pid, status);
   console("start3(): done\n");
   Terminate(8);
   return 0;
} /* start3 */

int Child1(char *arg)
{
   int pid;
   int status;

   GetPID(&pid);
   console("%s(): starting, pid = %d\n", arg, pid);
   Spawn("Child2", Child2, "Child2", USLOSS_MIN_STACK, 2, &pid);
   console("%s(): spawned process %d\n", arg, pid);
   Wait(&pid, &status);
   Spawn("Child2b", Child2b, "Child2b", USLOSS_MIN_STACK, 2, &pid);
   console("%s(): spawned process %d\n", arg, pid);
   Wait(&pid, &status);
   console("%s(): child %d returned status of %d\n", arg, pid, status);
   console("%s(): done\n", arg);
   Terminate(9);

   return 0;
} /* Child1 */

int Child2(char *arg)
{
   int pid, i;

   GetPID(&pid);
   console("%s(): starting, pid = %d\n", arg, pid);
   for(i = 0; i != MAXPROC; i++) {
      Spawn("Child2a", Child2a, "Child2a", USLOSS_MIN_STACK, 3, &pid);
      if (pid >= 0)
         console("%s(): spawned process %d\n", arg, pid);
      else
         console("%s(): spawned attempt failed, pid = %d\n", arg, pid);
   }
   console("%s(): Terminating me and all my children\n", arg);
   Terminate(10);

    return 0;
} /* Child2 */

int Child2a(char *arg)
{
   int pid;

   GetPID(&pid);
   console("%s(): starting the code for Child2a: pid=%d\n", arg, pid);

   Terminate(11);
   return 0;
} /* Child2a */

int Child2b(char *arg)
{
   int pid, status;

   GetPID(&pid);
   console("%s(): starting, pid = %d\n", arg, pid);
   Spawn("Child2c", Child2c, "Child2c", USLOSS_MIN_STACK, 1, &pid);
   console("%s(): spawned process %d\n", arg, pid);
   Wait(&pid, &status);

   Terminate(50);
   return 0;
} /* Child2b */

int Child2c(char *arg)
{
    console("%s(): starting the code for Child2c\n", arg);

    Terminate(11);
    return 0;
} /* Child2c */
