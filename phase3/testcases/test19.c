/* Check Time of day */
#include <usyscall.h>
#include <libuser.h>
#include <assert.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

int start3(char *arg)
{
   int i, start, end, elapsed;

   console("start3(): started\n");
   GetTimeofDay(&start);
   for(i=0; i<100000; i++)
      ;
   GetTimeofDay(&end);
   elapsed = end - start;
   console("start3(): elapsed time = %d\n",elapsed);
  
   Terminate(0);

   return 0;
} /* start3 */







