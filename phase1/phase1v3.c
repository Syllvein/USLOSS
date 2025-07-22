/* ------------------------------------------------------------------------
   phase1.c

   CSCV 452

   ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <phase1.h>
#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int is_zapped(void);
int read_cur_start_time(void);
int sentinel (char * dummy);
int zap (int pid_to_zap);
extern int start1 (char *);
static int GetNextPid();
static void enableInterrupts();
static void check_deadlock();
void clock_handler(int dev, void *arg);
void dispatcher(void);
void dump_processes(void);
void launch();
void ListAdd(ProcList *pList, struct proc_struct *pProc);
void ListInit(ProcList *pList);
void ListPrint(ProcList *pList);
void time_slice(void);
struct proc_struct *ListPop(ProcList *pList);
proc_ptr GetNextProcess();

/* -------------------------- Globals ------------------------------------- */

/* Debugging global variable... */
int debugflag = 1;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
ProcList ReadyList[6];

/* current process ID */
proc_ptr Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;
numProc = 0;

// Human readable format for process status.
static char *statusText[] = {"EMPTY", "READY", "RUNNING", "QUIT", 
                              "JOIN_BLOCKED", "ZAPPED", "RECORDED_QUIT"};

/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
	     Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
   int i;      /* loop index */
   int result; /* value returned by call to fork1() */

   /* initialize the process table */

   /* Initialize the Ready list, etc. */
   if (DEBUG && debugflag)
   {
      console("startup(): initializing the Ready & JOIN_BLOCKED lists\n");
   }

   for (i = 0; i < HIGHEST_PRIORITY; i++)
   {
      ListInit(&ReadyList[i]);
   }

   /* Initialize the clock interrupt handler */
   int_vec[CLOCK_INT] = clock_handler;

   /* startup a sentinel process */
   if (DEBUG && debugflag)
   {
      console("startup(): calling fork1() for sentinel\n");
   }
   
   result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                   SENTINELPRIORITY);
   
   if (result < 0)
   {
      if (DEBUG && debugflag)
      {
         console("startup(): fork1 of sentinel returned error, halting...\n");
         halt(1);
      }
   }

   /* start the test process */
   if (DEBUG && debugflag)
   {
      console("startup(): calling fork1() for start1\n");
   }
   
   result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
   
   if (result < 0)
   {
      console("startup(): fork1 for start1 returned an error, halting...\n");
      halt(1);
   }

   console("startup(): Should not see this message!\n");
   console("Returned from fork1 call that created start1\n");

   return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
   if (DEBUG && debugflag)
   {
      console("in finish...\n");
   }
} /* finish */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*f)(char *), char *arg, int stacksize, int priority)
{
   int newPid;
   proc_ptr pEntry;

   if (DEBUG && debugflag)
   {
      console("fork1(): creating process %s\n", name);
   }

   /* test if in kernel mode; halt if in user mode */
   if((PSR_CURRENT_MODE & psr_get()) == 0)
   {
      //not in kernel mode
      console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
      halt(1);
   }

   newPid = GetNextPid();
   pEntry = &ProcTable[newPid%MAXPROC];

   pEntry->stack = malloc(stacksize);
   pEntry->stacksize = stacksize;
   pEntry->status = READY;
   pEntry->priority = priority;
   pEntry->pid = newPid;
   pEntry->parent_proc_ptr = Current;

   /* Return if stack size is too small */
   if (stacksize < USLOSS_MIN_STACK)
   {
      console("Stacksize is too small. Halting...\n");
      halt(1);
   }

   /* find an empty slot in the process table */

   /* fill-in entry in process table */
   if (strlen(name) >= (MAXNAME - 1))
   {
      console("fork1(): Process name is too long.  Halting...\n");
      halt(1);
   }

   strcpy(pEntry->name, name);
   pEntry->start_func = f;

   if (arg == NULL)
   {
      pEntry->start_arg[0] = '\0';
   }

   else if (strlen(arg) >= (MAXARG - 1))
   {
      console("fork1(): argument too long.  Halting...\n");
      halt(1);
   }

   else
   {
      strcpy(pEntry->start_arg, arg);
   }

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
   context_init(&(pEntry->state), psr_get(),
                pEntry->stack, pEntry->stacksize, launch);
   
   ListAdd(&ReadyList[priority-1], pEntry);

   if (Current != NULL)
   {
      if (Current->child_proc_ptr != NULL)
      {
         pEntry = Current->child_proc_ptr;

         while (pEntry->next_sibling_ptr != NULL)
         {
            pEntry = pEntry->next_sibling_ptr;
         }

         pEntry->next_sibling_ptr = &ProcTable[newPid%MAXPROC];
      }

      else
      {
         Current->child_proc_ptr = &ProcTable[newPid%MAXPROC];
      }
   }

   else
   {
      ProcTable[newPid%MAXPROC].parent_proc_ptr = Current;
   }

   /* for future phase(s) */
   p1_fork(pEntry->pid);

   // Call dispatcher
   if (pEntry->priority != SENTINELPRIORITY)
   {
      dispatcher();
   }

   return newPid;
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
   int result;

   if (DEBUG && debugflag)
   {
      console("launch(): started\n");
   }

   /* Enable interrupts */
   enableInterrupts();

   /* Call the function passed to fork1, and capture its return value */
   result = Current->start_func(Current->start_arg);

   if (DEBUG && debugflag)
   {
      console("Process %d returned to launch\n", Current->pid);
   }

   quit(result);
} /* launch */

/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
		-1 if the process was zapped in the join
		-2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and JOIN_BLOCKED.
   ------------------------------------------------------------------------ */
int join(int *code)
{
   int pid = -2;

   if (Current->child_proc_ptr != NULL)
   {
      if (Current->child_proc_ptr->status == QUIT && 
            Current->child_proc_ptr->next_sibling_ptr == NULL)
      {
         pid = Current->child_proc_ptr->pid;
         *code = Current->child_proc_ptr->exitCode;
      }

      else if (Current->child_proc_ptr->status == QUIT || 
               Current->child_proc_ptr->status == RECORDED_QUIT && 
               Current->child_proc_ptr->next_sibling_ptr != NULL)
      {
         if (Current->child_proc_ptr->status == QUIT)
         {
            Current->child_proc_ptr->status = RECORDED_QUIT;
            pid = Current->child_proc_ptr->pid;
            *code = Current->child_proc_ptr->exitCode;
         }

         else if (Current->child_proc_ptr->next_sibling_ptr->status == QUIT)
         {
            Current->child_proc_ptr->next_sibling_ptr->status = RECORDED_QUIT;
            pid = Current->child_proc_ptr->next_sibling_ptr->pid;
            *code = Current->child_proc_ptr->next_sibling_ptr->exitCode;
         }

         else
         {
         Current->status = JOIN_BLOCKED;
         dispatcher();
         Current->child_proc_ptr->next_sibling_ptr->status = RECORDED_QUIT;
         pid = Current->child_proc_ptr->next_sibling_ptr->pid;
         *code = Current->child_proc_ptr->next_sibling_ptr->exitCode;
         }
      }

      else
      {
         Current->status = JOIN_BLOCKED;
         dispatcher();
         Current->child_proc_ptr->status = RECORDED_QUIT;
         pid = Current->child_proc_ptr->pid;
         *code = Current->child_proc_ptr->exitCode;
      }
   }

   return pid;
} /* join */

/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code)
{
   proc_ptr parent;
   p1_quit(Current->pid);

   Current->exitCode = code;
   Current->status = QUIT;
   parent = Current->parent_proc_ptr;

   if (parent != NULL)
   {
      if (parent->status == JOIN_BLOCKED)
      {
         parent->status = READY;
         int priority = parent->priority;
         ListAdd(&ReadyList[priority-1], parent);
      }
   }

   dispatcher();
} /* quit */

/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes. The process with the highest
             priority (the first on the ready list) is scheduled to
             run. The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
   int switchProcesses = 0;
   int i;
   proc_ptr nextProcess = NULL;

   if (Current != NULL && Current->status == RUNNING)
   {
      if (Current->priority > Current->child_proc_ptr->priority && 
            Current->child_proc_ptr-> status != QUIT)
         {
            Current->status = READY;
            ListAdd(&ReadyList[Current->priority -1], Current);
            switchProcesses = 1;
         }

      else
      {
         for (i = 0; i < Current->priority -1; ++i)
         {
            if (ReadyList[i].count > 0)
            {
               switchProcesses = 1;
            }
         }
      }
   }

   else
   {
      switchProcesses = 1;
   }

   if (switchProcesses)
   {
      nextProcess = GetNextProcess();
      
      if (nextProcess == NULL)
      {
         console("ERROR! No Process!\n");
         halt(1);
      }
   }
} /* dispatcher */

/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
	     processes are JOIN_BLOCKED.  The other is to detect and report
	     simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
		   and halt.
   ----------------------------------------------------------------------- */
int sentinel (char * dummy)
{
   if (DEBUG && debugflag)
   {
      console("sentinel(): called\n");
   }

   while (1)
   {
      check_deadlock();
      waitint();
   }
} /* sentinel */

/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
   int processes = 0;

   for (int i = 0; i < MAXPROC; i++)
   {
      if (ProcTable[i].status != EMPTY)
      {
         if (ProcTable[i].status != QUIT && ProcTable[i].status != RECORDED_QUIT)
         {
            processes++;
         }
      }
   }

   if (processes > 1)
   {
      console("Number of processes greater than 1! Halting!\n");
      halt(1);
   }

   else
   {
      console("All processes complete.\n");
      halt(0);
   }
} /* check_deadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
  /* turn the interrupts OFF if we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0)
  {
      //not in kernel mode
      console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
      halt(1);
   } 
  
  else
  {
      /* We ARE in kernel mode */
      psr_set( psr_get() & ~PSR_CURRENT_INT );
  }
} /* disableInterrupts */

static void enableInterrupts()
{
   // Check if not in Kernel mode.
   if (PSR_CURRENT_MODE & psr_get() == 0)
   {
      console("Not in Kernel Mode! Halting...");
      halt(1);
   }

   // If in kernel mode.
   else
   {
      int curPsr = psr_get();
      curPsr = curPsr | PSR_CURRENT_INT;
      psr_set(curPsr);
   }
}

void clock_handler(int dev, void *arg)
{
   unsigned int elapsedTime;
   unsigned int curTime = sys_clock();

   elapsedTime = curTime - Current->cpuTime;
}

void time_slice(void)
{

}

int read_cur_start_time(void)
{

}

int zap (int pid_to_zap)
{
   int result = 0;
   int zapperPid = Current->pid;
   int procSlot = pid_to_zap%MAXPROC;

   // Halt if attempting to zap self
   if (pid_to_zap == zapperPid)
   {
      console("Zap: Process attempting to zap itself.\n");
      halt(1);
   }

   disableInterrupts();

   if (ProcTable[procSlot].status == EMPTY || ProcTable[procSlot].pid != pid_to_zap)
   {
      console("Zap: Attempting to zap a process that doesn't exist.\n");
      halt(1);
   }

   if (ProcTable[procSlot].status != QUIT)
   {
      ListAdd(&ProcTable[pid_to_zap%MAXPROC].next_zapped_ptr, Current);
      Current->status = ZAPPED;

      dispatcher();

      // Check if we were zapped while zapping.
      if (is_zapped())
      {
         console("zap(): Process zapped while zapping\n");
         result = -1;
      }
   }

   enableInterrupts();
   return result;
}

int is_zapped(void)
{
   if (Current->status == ZAPPED)
   {
      return 1;
   }

   else
   {
      return 0;
   }
}

static int GetNextPid()
{
   int newPid = -1;
   int procSlot = next_pid % MAXPROC;

   if (numProc < MAXPROC)
   {
      while (numProc < MAXPROC && ProcTable[procSlot].status != EMPTY)
      {
         next_pid++;
         procSlot = next_pid % MAXPROC;
      }

      newPid = next_pid++;
   }

   return newPid;
}

proc_ptr GetNextProcess()
{
   proc_ptr nextProcess = NULL;
   proc_ptr prevProcess = NULL;
   int i;

   for (i = 0; i <= SENTINELPRIORITY - 1; ++i)
   {
      if (ReadyList[i].count != 0)
      {
         nextProcess = ListPop(&ReadyList[i]);
         break;
      }
   }

   prevProcess = Current;
   Current = nextProcess;

   if (prevProcess != NULL && prevProcess->status == RUNNING)
   {
      prevProcess->status = READY;
      ListAdd(&ReadyList[prevProcess->priority -1], prevProcess);
   }

   nextProcess->status = RUNNING;

   if (prevProcess == NULL)
   {
      context_switch(NULL, &nextProcess->state);
   }
   
   else
   {
      context_switch(&prevProcess->state, &nextProcess->state);
   }
   
   // Pick the next process to run
   p1_switch(Current->pid, nextProcess->pid);

   return nextProcess;
}

struct proc_struct *ListPop(ProcList *pList)
{
   struct proc_struct *popProcess = NULL;

   if (pList->count > 0)
   {
      popProcess = pList->pHead;

      if (pList->pHead != NULL)
      {
         if (pList->pHead->parent_proc_ptr != NULL)
         {
            pList->pHead = pList->pHead->next_sibling_ptr;
         }

         else if (pList->pHead->zapped > 0)
         {
            pList->pHead = pList->pHead->next_zapped_ptr;
         }

         else
         {
            pList->pHead = pList->pHead->next_proc_ptr;
         }
      }

      else
      {
         pList->pHead = pList->pHead->next_proc_ptr;
      }

      pList->count--;
   }

   return popProcess;
}

/* ------------------------------------------------------------------------
   ListInit

   Purpose -      Initializes the list structure 
   Parameters -   pList - The list to initialize
   Returns -      None
   Side Effects - 
   ----------------------------------------------------------------------- */
void ListInit(ProcList *pList)
{
   pList->count = 0;
   pList->pHead = pList->pTail = NULL;
}

/* ------------------------------------------------------------------------
   ListAdd

   Purpose -      Adds process to the tail of the list 
   Parameters -   pList - The list to add to
                  pProc - The node to add to the list
   Returns -      None
   Side Effects - 
   ----------------------------------------------------------------------- */
void ListAdd(ProcList *pList, struct proc_struct *pProc)
{

   if (pList->pHead == NULL)
   {
      // if the list is empty, set head and tail pointers to the only node
      pList->pHead = pList->pTail = pProc;
   }

   else
   {
      if (pList->pHead->parent_proc_ptr != NULL)
      {
         pList->pHead = pList->pHead->next_sibling_ptr;
      }

      else if (pList->pHead->zapped > 0)
      {
         pList->pHead = pList->pHead->next_zapped_ptr;
      }

      else
      {
         pList->pHead = pList->pHead->next_proc_ptr;
      }

      pList->pTail = pProc;
   }
   
   pList->count++;
}

/* ------------------------------------------------------------------------
   ListPrint

   Purpose -      Displays a process list.
   Parameters -   pList - The list of processes to print
   Returns -      None
   Side Effects - 
   ----------------------------------------------------------------------- */
void ListPrint(ProcList *pList)
{
   struct proc_struct *pNode;
   pNode = pList->pHead;

   while (pNode != NULL)
   {
      printf("%d: %s\n", pNode->pid, pNode->name);
      // add from the tail, set the next pointer then move the tail
      
      if (pList->pHead->parent_proc_ptr != NULL)
      {
         pNode = pNode->next_sibling_ptr;
      }

      else if (pList->pHead->zapped > 0)
      {
         pNode = pNode->next_zapped_ptr;
      }

      else
      {
         pNode = pNode->next_proc_ptr;
      }
   }
}

void dump_processes(void)
{
   int parentPid = 0;
   int numKid = 0;
   proc_ptr nextSib;

   console("%-7s %-8s %-9s %-13s %-8s %-8s %-8s\n",
            "PID", "Parent", "Priority", "Status", "# Kids", "CPUtime", "Name");

   for (int i = 0; i < MAXPROC; i++)
   {
      if (ProcTable[i].status == 0)
      {
         ProcTable[i].pid = -1;
         ProcTable[i].priority = -1;
         ProcTable[i].pid = -1;
      }

      if (ProcTable[i].parent_proc_ptr == NULL)
      {
         parentPid = -1;
      }

      else
      {
         parentPid = ProcTable[i].parent_proc_ptr->pid;
      }

      if (ProcTable[i].child_proc_ptr != NULL)
      {
         numKid++;

         if (ProcTable[i].child_proc_ptr->next_sibling_ptr != NULL)
         {
            numKid++;
            nextSib = ProcTable[i].child_proc_ptr->next_sibling_ptr;
            nextSib = nextSib->next_sibling_ptr;
            while (nextSib != NULL)
            {
               numKid++;
               nextSib = nextSib->next_sibling_ptr;
            }
         }
      }

      else
      {
         numKid = 0;
      }

      console("%-7d %-8d %-9d %-13s %-8d %-8d %-8s\n",
            ProcTable[i].pid, parentPid, ProcTable[i].priority, 
            statusText[ProcTable[i].status], numKid, ProcTable[i].cpuTime, ProcTable[i].name);
   }
}