/* Check spawn and it's return parameters work.  */

#include <stdlib.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>

int Child1(char *arg) 
{
  console("Child1(): started\n");
  Terminate(32);

  return 0;
}


int start3(char *arg)
{
  int pid,id;

  Spawn("Child1", Child1, NULL, USLOSS_MIN_STACK, 4, &pid);
  Wait(&pid, &id);
  assert(id == 32);
  console("start3(): Done.\n");
  Terminate(0);

  return 0;
}







