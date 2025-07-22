/*
  Check that spawn and it's return parameters work. 
  Also check if start3 is in user mode.
*/
#include <usyscall.h>
#include <libuser.h>
#include <stdio.h>
#include <stdlib.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

#include <assert.h>



int Child1(char *arg) 
{
  if(psr_get() & PSR_CURRENT_MODE){
    printf("Child1(): not in user mode\n");
    exit(1);
  }
  console("Child1(): starting\n");
  Terminate(32);
  return 0;
}


int start3(char *arg)
{
  int pid,id;

  printf("start3(): started\n");
  if(psr_get() & PSR_CURRENT_MODE){
    printf("start3 not in user mode\n");
    exit(1);
  }
  Spawn("Child1", Child1, NULL, USLOSS_MIN_STACK, 4, &pid);
  printf("start3(): fork %d\n", pid);
  Wait(&pid, &id);
  assert(id == 32);
  console("start3(): Done.\n");
  Terminate(0);

  return 0;
}







