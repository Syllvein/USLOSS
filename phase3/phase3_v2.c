#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>
#include <sems.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------- Prototypes ----------------------------------- */
extern int start3(char *);
int cputime_real(int *time);
int getPID_real(int *pid);
int GetSemID();
int gettimeofday_real(int *time);
int semcreate_real(int init_value);
int semfree_real(int semaphore);
int semp_real(int semaphore);
int semv_real(int semaphore);
int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority);
int start2(char *); 
int wait_real(int *status);
static int spawn_launch(char *arg);
void check_kernel_mode (char *procName);
void InitTables();
void ListAdd(List *pList, void* pNode);
static void nullsys3();
static void sysCall(sysargs *pSysarg);
struct UserProcess *ListPop(List *pList);
void terminate_real(int exit_code);

/* -------------------------- Globals ------------------------------------- */
UserProcess UserProcTable[MAXPROC];
Semaphore SemTable[MAXSEMS];

/* ------------------------------------------------------------------------
   Name         -   start2
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int start2(char *arg)
{
    int	pid;
    int	status;

    check_kernel_mode("start2");

    /*
     * Data structure initialization as needed...
     */
    for(int i = 0; i < MAXSYSCALLS; i++)
    {
        sys_vec[i] = sysCall;
    }

    InitTables();

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscall_handler; spawn_real is the function that
     * contains the implementation and is called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes usyscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawn_real().
     *
     * Here, we only call spawn_real(), since we are already in kernel mode.
     *
     * spawn_real() will create the process by using a call to fork1 to
     * create a process executing the code in spawn_launch().  spawn_real()
     * and spawn_launch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawn_real() will
     * return to the original caller of Spawn, while spawn_launch() will
     * begin executing the function passed to Spawn. spawn_launch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawn_real() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and 
     * return to the user code that called Spawn.
     */
    pid = spawn_real("start3", start3, NULL, 4*USLOSS_MIN_STACK, 3);

    if (pid == -1)
    {
        quit(-1);
    }

    pid = wait_real(&status);

    if (pid == -1)
    {
        quit(-1);
    }

    quit(0);
    return 0;
} /* start2 */


/* ------------------------------------------------------------------------
   Name         -   sysCall
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
static void sysCall(sysargs *pSysarg)
{
    int result;
    int status;

    switch (pSysarg->number)
    {
        case SYS_CPUTIME:
            result = cputime_real((int *)pSysarg->arg1);
            pSysarg->arg1 = (void *)result;
            break;

        case SYS_GETPID:
            result = getPID_real((int *)pSysarg->arg1);
            pSysarg->arg1 = (void *)result;
            break;

        case SYS_GETTIMEOFDAY:
            result = gettimeofday_real((int *)pSysarg->arg1);
            pSysarg->arg1 = (void *)result;
            break;

        case SYS_SEMCREATE:
            result = semcreate_real((int)pSysarg->arg1);
            
            pSysarg->arg1 = (void *) result;
            if (result < 0)
            {
                pSysarg->arg4 = (void *) result;
            }
            
            else
            {
                pSysarg->arg4 = (void *) 0;
            }
            
            break;

        case SYS_SEMFREE:
            result = semfree_real((int)pSysarg->arg1);
            pSysarg->arg1 = (void *)result;
            break;

        case SYS_SEMP:
            result = semp_real((int)pSysarg->arg1);
            pSysarg->arg1 = (void *)result;
            break;

        case SYS_SEMV:
            result = semv_real((int)pSysarg->arg1);
            pSysarg->arg1 = (void *)result;
            break;

        case SYS_SPAWN:
            result = spawn_real((char *)pSysarg->arg5, 
                                (int (*)(char *))(pSysarg->arg1), 
                                (char *)pSysarg->arg2, 
                                (int)pSysarg->arg3, 
                                (int)pSysarg->arg4);

            pSysarg->arg1 = (void *) result;
            break;

        case SYS_TERMINATE:
            terminate_real((int)pSysarg->arg1);
            break;

        case SYS_WAIT:
            result = wait_real(&status);
            pSysarg->arg1 = (void *)result;
            pSysarg->arg2 = (void *)status;
            break;

        default:
            nullsys3(pSysarg->number);
            break;
    }

} /* sysCall */


/* ------------------------------------------------------------------------
   Name         -   spawn_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority)
{
    int kidpid;
    user_ptr parent_ptr;
    user_ptr child_ptr;

    /* create our child */
    kidpid = fork1(name, spawn_launch, arg, stack_size, priority);
    child_ptr = &UserProcTable[kidpid%MAXPROC];

    if (child_ptr->status == EMPTY)
    {
        child_ptr->status = ITEM_IN_USE;
        child_ptr->mboxStartup = MboxCreate (0, 0);
    }

    //more to check the kidpid and put the new process data to the process table
    child_ptr->pid = kidpid;
    child_ptr->startFunc = func;
    child_ptr->stackSize = stack_size;
    child_ptr->priority = priority;
    parent_ptr = &UserProcTable[getpid()%MAXPROC];

    if (name != NULL)
    {
        strncpy(child_ptr->name, name, MAXNAME - 1);
    }

    if (arg != NULL)
    {
        strncpy(child_ptr->arg, arg, MAXARG - 1);
    }

    if (parent_ptr->startFunc != NULL)
    {
        ListAdd(&parent_ptr->children, child_ptr);
        child_ptr->ppid = parent_ptr->pid;
    }

    //Then synchronize with the child using a mailbox
    MboxCondSend(child_ptr->mboxStartup, NULL, 0);

    return kidpid;
    
} /* spawn_real */


/* ------------------------------------------------------------------------
   Name         -   spawn_launch
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
static int spawn_launch(char *arg)
{
    int index;
    int result;
    user_ptr proc_ptr;

    index = getpid() % MAXPROC;
    proc_ptr = &UserProcTable[index];

    if (proc_ptr->startFunc == NULL)
    {
        proc_ptr->status = ITEM_IN_USE;
        proc_ptr->mboxStartup = MboxCreate(0, 0);
        MboxReceive(proc_ptr->mboxStartup, NULL, 0);
    }

    //You should synchronize with the parent here,
    //which function to call?
    //Then get the start function and its argument
    if (!is_zapped())
    {
        //Then set up user mode
        psr_set(psr_get() & ~PSR_CURRENT_MODE);
        result = UserProcTable[index].startFunc(UserProcTable[index].arg);
        Terminate(result);
    }

    else
    {
        result = UserProcTable[index].startFunc(arg);
        terminate_real(result);
    }

    console("spawn_launch(): should not see this message following Terminate!\n");
    return 0;

} /* spawn_launch */


/* ------------------------------------------------------------------------
   Name         -   wait_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int wait_real(int *status)
{
    int pid;
    int parent_pid;
    user_ptr pop_ptr;

    pid = join(status);

    if (pid < 0)
    {
        Terminate(pid);
    }

    if ((parent_pid = &UserProcTable[pid%MAXPROC].ppid) != 0)
    {
        pop_ptr = ListPop(&UserProcTable[parent_pid%MAXPROC]);
    }

    return pid;
} /* wait_real */


/* ------------------------------------------------------------------------
   Name         -   cputime_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int cputime_real(int *time)
{

} /* cputime_real */


/* ------------------------------------------------------------------------
   Name         -   getPID_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int getPID_real(int *pid)
{

} /* getPID_real */


/* ------------------------------------------------------------------------
   Name         -   gettimeofday_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int gettimeofday_real(int *time)
{

} /* gettimeofday_real */


/* ------------------------------------------------------------------------
   Name         -   semcreate_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int semcreate_real(int init_value)
{
    int sem_id;

    sem_id = GetSemID();

    if (sem_id < 0)
    {
        return -1;
    }

    SemTable[sem_id].sem_id = sem_id;
    SemTable[sem_id].status = ITEM_IN_USE;
    SemTable[sem_id].mbox_id = MboxCreate(0, 0);

    return sem_id;
} /* semcreate_real */


/* ------------------------------------------------------------------------
   Name         -   semfree_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int semfree_real(int semaphore)
{

} /* semfree_real */


/* ------------------------------------------------------------------------
   Name         -   semp_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int semp_real(int semaphore)
{

} /* semp_real */


/* ------------------------------------------------------------------------
   Name         -   semv_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int semv_real(int semaphore)
{

} /* semv_real */


/* ------------------------------------------------------------------------
   Name         -   terminate_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
void terminate_real(int exit_code)
{
    user_ptr proc_ptr;
    user_ptr child_ptr;

    proc_ptr = &UserProcTable[getpid()%MAXPROC];

    proc_ptr->status = EMPTY;
    quit(exit_code);

} /* terminate_real */


/* ------------------------------------------------------------------------
   Name         -   nullsys3
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
static void nullsys3(sysargs *pSysarg)
{
    console("nullsys3(): Invalid syscall %d\n", pSysarg->number);
    console("nullsys3(): process %d terminating\n", getpid());
    terminate_real(1);
} /* nullsys3 */
   

/* ------------------------------------------------------------------------
   Name         -   InitTables
   Purpose      -   memsets each index of a Table.
   Parameters   -   None
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
void InitTables()
{
    // Clear junk values from the SemTable at index i.
    for (int i = 0; i < MAXSEMS; i++)
    {
        memset(&SemTable[i], 0, sizeof(SemTable[i]));
    }
    
    // Clear junk values from the UserProcTable at index j.
    for (int j = 0; j < MAXPROC; j++)
    {
        memset(&UserProcTable[j], 0, sizeof(UserProcTable[j]));
    }
} /* InitTables */


/* ------------------------------------------------------------------------
   Name         -   check_kernel_mode
   Purpose      -   To check if a process is in kernel mode or not.
   Parameters   -   procName - The name of the process to check.
   Returns      -   None
   Side Effects -   Halts if not in kernel mode.
   ----------------------------------------------------------------------- */
void check_kernel_mode (char *procName)
{
    // Check if not in kernel mode.
    if ((PSR_CURRENT_MODE & psr_get()) == 0)
    {
        console("Not in Kernel Mode! Halting...");
        halt(1);
    }
} /* check_kernel_mode */


/* ------------------------------------------------------------------------
   Name         -   GetSemID
   Purpose      -   Get the next empty semaphore.
   Parameters   -   None
   Returns      -   The index for the abailable semaphore or -2 if there
                    are no more slots available.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int GetSemID()
{
    // If mailslots are less than the maximum mailslots.
    for (int i = 0; i < MAXSEMS; i++)
    {
        // Search for an empty slot in the SemTable and return the ID. 
        if (SemTable[i].status == EMPTY)
        {
            SemTable[i].sem_id = i;
            return i;
        }
    }

    // Return -2 if there are no more empty slots.
    return -2;
}/* GetSemID */


/* ------------------------------------------------------------------------
   Name         -   ListPop
   Purpose      -   Removes the first node in the list
   Parameters   -   pList - The list to add to
   Returns      -   Pointer to the first node or NULL if list is empty
   Side Effects - 
   ----------------------------------------------------------------------- */
struct UserProcess *ListPop(List *pList)
{
   struct UserProcess *popProcess = NULL;

    // If there is a process to pop
    if (pList->count > 0)
    {
        // Pop the head of the list.
        popProcess = pList->pHead;

        // If the head isn't NULL.
        if (pList->pHead != NULL)
        {
            // Move the head to the next pointer.
            pList->pHead = pList->pHead->next_ptr;
        }

        // Decrement the count.
        pList->count--;

        // If the head is NULL, set the tail to NULL too.
        if (pList->pHead == NULL)
        {
            pList->pTail = pList->pHead;
        }
    }

   return popProcess;
} /* ListPop */


/* ------------------------------------------------------------------------
   Name         -   ListAdd
   Purpose      -   Adds process to the tail of the list 
   Parameters   -   pList - The list to add to
                    pProc - The node to add to the list
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
void ListAdd(List *pList, void* pNode)
{
    // If this is the first item on the list.
    if (pList->count == 0)
    {
        // if the list is empty, set head and tail pointers to the only node
        pList->pHead = pList->pTail = pNode;
    }

    else
    {
        // Assign the pNode to the next pointer of the tail
        // and move the tail.
        pList->pTail->next_ptr = pNode;
        pList->pTail = pList->pTail->next_ptr;
    }

    // Increase the count in the SlotList.
    pList->count++;
} /* ListAdd */