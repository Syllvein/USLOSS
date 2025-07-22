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
int GetSemID();
int semcreate_real(int init_value);
int semfree_real(int semaphore);
int semp_real(int semaphore);
int semv_real(int semaphore);
int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority);
int start2(char *); 
int wait_real(int *status);
void check_kernel_mode (char *procName);
void InitTables();
void ListAdd(List *pList, void* pNode);
void terminate_real(int exit_code);
static int spawn_launch(char *arg);
static void nullsys3();
static void sysCall(sysargs *pSysarg);
struct UserProcess *ListPop(List *pList);

/* -------------------------- Globals ------------------------------------- */
UserProcess UserProcTable[MAXPROC]; // UserProcessTable
Semaphore SemTable[MAXSEMS];        // SemTable
int semID = 0;                      // Assigning unique semaphore IDs.

/* ------------------------------------------------------------------------
   Name         -   start2
   Purpose      -   Spawns start3 and intitalizes sys_vec and the SemTable
                    and the UserProcessTable.
   Parameters   -   arg - Function argument.
   Returns      -   Returns 0
   Side Effects -   
   ----------------------------------------------------------------------- */
int start2(char *arg)
{
    int	pid;
    int	status;

    // Check if start2 is in kernel mode.
    check_kernel_mode("start2");

    // Initialize sys_vec with the syscall function.
    for(int i = 0; i < MAXSYSCALLS; i++)
    {
        sys_vec[i] = sysCall;
    }

    // Initialize the user process table and the smeaphore table.
    InitTables();

    // Spawn start3
    pid = spawn_real("start3", start3, NULL, 4*USLOSS_MIN_STACK, 3);

    // Check if there was an error when creating start3.
    if (pid == -1)
    {
        quit(-1);
    }

    // Call start3 to wait.
    pid = wait_real(&status);

    // If start3 failed to join with start2, then quit.
    if (pid == -1)
    {
        quit(-1);
    }

    quit(0);
    return 0;
} /* start2 */


/* ------------------------------------------------------------------------
   Name         -   sysCall
   Purpose      -   Manages all the sytem calls for phase3.
   Parameters   -   *pSysarg - The argument passed to sysCall.
   Returns      -   None
   Side Effects -   
   ----------------------------------------------------------------------- */
static void sysCall(sysargs *pSysarg)
{
    int result;
    int status;

    // Check the number and find the sysCall opcode that matches.
    switch (pSysarg->number)
    {
        // If pSysarg->number is 21.
        case SYS_CPUTIME:
            // Get the readtime value and assign it to pSysarg->arg1.
            pSysarg->arg1 = ((int *)readtime());
            break;

        // If pSysarg->number is 22.
        case SYS_GETPID:
            // Call the getpid function and assign it to pSysarg->arg1.
            pSysarg->arg1 = ((int *)getpid());
            break;

        // If pSysarg->number is 20.
        case SYS_GETTIMEOFDAY:
            // Call sys_clock and assign the value to pSysarg->arg1.
            pSysarg->arg1 = ((int *)sys_clock());
            break;

        // If pSysarg->number is 16.
        case SYS_SEMCREATE:
            // Call semcreat_real and assign the value to result.
            result = semcreate_real((int)pSysarg->arg1);
            
            // pSysarg->arg1 equals result.
            pSysarg->arg1 = (void *) result;
            
            // If result is less than zero, assign result to pSysarg->arg4.
            if (result < 0)
            {
                pSysarg->arg4 = (void *) result;
            }
            
            // Otherwise pSysarg->arg4 equals 0.
            else
            {
                pSysarg->arg4 = (void *) 0;
            }
            break;

        // If pSysarg->number is 19.
        case SYS_SEMFREE:
            // Call semfree_real and assign the value to result.
            // pSysarg->arg4 then equals result.
            result = semfree_real((int)pSysarg->arg1);
            pSysarg->arg4 = (void *)result;
            break;

        // If pSysarg->number is 17.
        case SYS_SEMP:
            // Call semp_real and assign the value to result.
            // pSysarg->arg4 then equals result.
            result = semp_real((int)pSysarg->arg1);
            pSysarg->arg4 = (void *)result;
            break;

        // If pSysarg->number is 18.
        case SYS_SEMV:
            // Call semv_real and assign the value to result.
            // pSysarg->arg4 then equals result.
            result = semv_real((int)pSysarg->arg1);
            pSysarg->arg4 = (void *)result;
            break;

        // If pSysarg->number is 3.
        case SYS_SPAWN:
            // Call spawn_real and assign the value to result.
            result = spawn_real((char *)pSysarg->arg5, 
                                (int (*)(char *))(pSysarg->arg1), 
                                (char *)pSysarg->arg2, 
                                (int)pSysarg->arg3, 
                                (int)pSysarg->arg4);

            // Assign result to pSysarg->arg1.
            pSysarg->arg1 = (void *) result;

            // If result is less than zero, assign result to pSysarg->arg4.
            if (result < 0)
            {
                pSysarg->arg4 = (void *)result;
            }

            // Otherwise pSysarg->arg4 equals 0.
            else
            {
                pSysarg->arg4 = (void *) 0;
            }
            break;

        // If pSysarg->number is 5.
        case SYS_TERMINATE:
            // Call terminate_real for pSysarg->arg1.
            terminate_real((int)pSysarg->arg1);
            break;

        // If pSysarg->number is 4.
        case SYS_WAIT:
            // Call wait_real and pass status to it, then assign the value 
            // returned to result, and finally assign result to pSysarg->arg1 
            // and status to pSysarg->arg2.
            result = wait_real(&status);
            pSysarg->arg1 = (void *)result;
            pSysarg->arg2 = (void *)status;
            break;

        // For everything else call nullsys3 function and pass pSysarg->number.
        default:
            nullsys3(pSysarg->number);
            break;
    }

    // Switch to user mode.
    psr_set(psr_get() & ~PSR_CURRENT_MODE);
} /* sysCall */


/* ------------------------------------------------------------------------
   Name         -   spawn_real
   Purpose      -   Creates a user level process.
   Parameters   -   name - Child process name.
                    func - Address of the child process.
                    arg - Function argument that will be used in syscall.
                    stack_size - The stack size.
                    priority - The Process priority.
   Returns      -   kidpid for the child process or -1 if it couldn't create
                    the process.
   Side Effects -   
   ----------------------------------------------------------------------- */
int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority)
{
    int kidpid;
    user_ptr parent_ptr;
    user_ptr child_ptr;

    // Create a child process.
    kidpid = fork1(name, spawn_launch, arg, stack_size, priority);

    // If there is an error return.
    if (kidpid < 0)
    {
        return kidpid;
    }

    // Create a pointer to the slot in the UserProcTable for the child.
    child_ptr = &UserProcTable[kidpid%MAXPROC];

    // If the child's status is empty.
    if (child_ptr->status == EMPTY)
    {
        // Switch status to IN_USE and creat a mailbox and get the ID.
        child_ptr->status = IN_USE;
        child_ptr->mboxStartup = MboxCreate (0, 0);
    }

    child_ptr->pid = kidpid;            // Assign kidpid to the process pid.
    child_ptr->startFunc = func;        // Assign func to startFunc.
    child_ptr->stackSize = stack_size;  // Assign stack_size to stackSize.
    child_ptr->priority = priority;     // Set the priority for the process.

    // Create a pointer to the slot in Table for the parent of the process.
    parent_ptr = &UserProcTable[getpid()%MAXPROC];

    // If name isn't NULL, copy name to the child process.
    if (name != NULL)
    {
        strncpy(child_ptr->name, name, MAXNAME - 1);
    }

    // If arg isn't NULL, assign arg to the child process.
    if (arg != NULL)
    {
        child_ptr->arg = arg;
    }

    // If parent startFunc isn't NULL.
    if (parent_ptr->startFunc != NULL)
    {
        // Add the child to the children list for the parent
        // and assign the parent's pid to the child ppid.
        ListAdd(&parent_ptr->children, child_ptr);
        child_ptr->ppid = parent_ptr->pid;
    }

    //Then synchronize with the child using a mailbox
    MboxCondSend(child_ptr->mboxStartup, NULL, 0);

    return kidpid;
} /* spawn_real */


/* ------------------------------------------------------------------------
   Name         -   spawn_launch
   Purpose      -   Calls the startFunc for the process being launched.
   Parameters   -   arg - Argument function.
   Returns      -   Returns 0.
   Side Effects -   
   ----------------------------------------------------------------------- */
static int spawn_launch(char *arg)
{
    int index;
    int result;
    user_ptr proc_ptr;

    // Get the pid of the process.
    index = getpid() % MAXPROC;
    // Set a pointer to the slot in the UserProcTable.
    proc_ptr = &UserProcTable[index];

    // If the child has a higher priority, it will need to populate it's
    // structure instead of it's parent doing it.
    if (proc_ptr->status == EMPTY)
    {
        // Change the process' status to IN_USE, creat a mailbox and assign
        // the id, the perform an mboxreceive.
        proc_ptr->status = IN_USE;
        proc_ptr->mboxStartup = MboxCreate(0, 0);
        MboxReceive(proc_ptr->mboxStartup, NULL, 0);
    }

    // If we're not zapped.
    if (!is_zapped())
    {
        // Then set up user mode, call the function, and terminate the process
        // after it has finished running. 
        psr_set(psr_get() & ~PSR_CURRENT_MODE);
        result = UserProcTable[index].startFunc(UserProcTable[index].arg);
        Terminate(result);
    }

    // If we were zapped, then terminate using terminate_real.
    else
    {
        terminate_real(0);
    }

    console("spawn_launch(): should not see this message following Terminate!\n");
    return 0;
} /* spawn_launch */


/* ------------------------------------------------------------------------
   Name         -   wait_real
   Purpose      -   If a process has any children, it should wait for them
                    to terminate before it does.
   Parameters   -   status - The exit code for the process terminating.
   Returns      -   pid of the process to terminate.
   Side Effects -   
   ----------------------------------------------------------------------- */
int wait_real(int *status)
{
    int pid;
    int parent_pid;
    user_ptr pop_ptr;

    // Call join and get the pid.
    pid = join(status);

    // Terminate if the pid is less than 0.
    if (pid < 0)
    {
        Terminate(pid);
    }

    // Get the parent pid for the user process if one exists.
    parent_pid = UserProcTable[pid%MAXPROC].ppid;

    // If the child is on the parent process' children list.
    if (UserProcTable[parent_pid%MAXPROC].children.count != 0)
    {
        // Pop the process off the parent's list and get the child's pid.
        pop_ptr = ListPop(&UserProcTable[parent_pid%MAXPROC].children);
        pid = pop_ptr->pid;
    }

    return pid;
} /* wait_real */


/* ------------------------------------------------------------------------
   Name         -   semcreate_real
   Purpose      -   Creates a user level semaphore.
   Parameters   -   init_value - The initial value for the semaphore.
   Returns      -   The id of the semaphore or -1 if the init_value is invalid
                    or the Semtable is full.
   Side Effects -   
   ----------------------------------------------------------------------- */
int semcreate_real(int init_value)
{
    int sem_id;

    // If the init_value is less than 0, return -1.
    if (init_value < 0)
    {
        return -1;
    }

    // Get a unique semaphore ID.
    sem_id = GetSemID();

    // If the SemTable is full, return -1.
    if (sem_id < 0)
    {
        return -1;
    }

    SemTable[sem_id].semValue = init_value;         // Set the intial value.
    SemTable[sem_id].sem_id = sem_id;               // Assign the sem_id.
    SemTable[sem_id].status = IN_USE;               // Set the status to IN_USE.
    SemTable[sem_id].semMbox = MboxCreate(1, 0);    // Create a mailbox and assign the mbox ID.

    return sem_id;
} /* semcreate_real */


/* ------------------------------------------------------------------------
   Name         -   semfree_real
   Purpose      -   Frees a semaphore.
   Parameters   -   semaphore - The sem_id of the semaphore being freed.
   Returns      -   Returns zero or -1 if the semaphore value in invalid.
   Side Effects -   
   ----------------------------------------------------------------------- */
int semfree_real(int semaphore)
{
    user_ptr pop_ptr;
    int retValue = 0;

    // If the semaphore value in less than 0, return -1.
    if (semaphore < 0)
    {
        retValue = -1;
        return retValue;
    }
    
    // Set the status of the semaphore to EMPTY so it will 
    // call terminate_real in semp_real.
    SemTable[semaphore].status = EMPTY;

    // If there are any processes on the waitingProcs list.
    while (SemTable[semaphore].waitingProcs.count != 0)
    {
        // Pop each process off the list, perform an mboxsend, 
        // and set the retValue to 1.
        pop_ptr = ListPop(&SemTable[semaphore].waitingProcs);
        MboxSend(pop_ptr->mboxStartup, NULL, 0);
        retValue = 1;
    }

    // Clear the SemTable slot.
    memset(&SemTable[semaphore], 0, sizeof(SemTable[semaphore]));

    return retValue;
} /* semfree_real */


/* ------------------------------------------------------------------------
   Name         -   semp_real
   Purpose      -   Performs a "P" operation on a semaphore.
   Parameters   -   semaphore - the sem_id of the semaphore.
   Returns      -   Returns -1 if the samaphore value is invalid or 0
                    if there are no errors.
   Side Effects -   
   ----------------------------------------------------------------------- */
int semp_real(int semaphore)
{
    int retValue = 0;
    int ppid;
    user_ptr block_ptr;

    // If the semaphore value is less than 0, return -1.
    if (semaphore < 0)
    {
        retValue = -1;
        return retValue;
    }

    // Perform an mboxsend to block.
    MboxSend(SemTable[semaphore].semMbox, NULL, 0);

    // if semvalue is greater than 0, perform an mboxreceive to unblock.
    if (SemTable[semaphore].semValue > 0)
    {
        MboxReceive(SemTable[semaphore].semMbox, NULL, 0);
    }

    else
    {
        // Get the parent pid and check if it has any children.
        ppid = UserProcTable[getpid()%MAXPROC].ppid;
        if (&UserProcTable[ppid%MAXPROC].children != 0)
        {
            // Pop a process off the list, add the process to the waitingproc list
            // and perform an mboxreceive for the semaphore and for the process.
            block_ptr = ListPop(&UserProcTable[ppid%MAXPROC].children);
            ListAdd(&SemTable[semaphore].waitingProcs, block_ptr);
            MboxReceive(SemTable[semaphore].semMbox, NULL, 0);
            MboxReceive(block_ptr->mboxStartup, NULL, 0);
        }

        else
        {
            // If the process isn't on the children list we add it to the waitingprocs,
            // list and perform an mboxreceive for the semaphore and for the process.
            block_ptr = &UserProcTable[getpid()%MAXPROC];
            ListAdd(&SemTable[semaphore].waitingProcs, block_ptr);
            MboxReceive(SemTable[semaphore].semMbox, NULL, 0);
            MboxReceive(block_ptr->mboxStartup, NULL, 0);
        }

        // If the status is EMPTY, call terminate_real.
        if (SemTable[semaphore].status == EMPTY)
        {
            terminate_real(1);
        }
    }

    // Decrement the semValue.
    SemTable[semaphore].semValue--;

    return retValue;
} /* semp_real */


/* ------------------------------------------------------------------------
   Name         -   semv_real
   Purpose      -   Performs a "V" operation on a semaphore.
   Parameters   -   semaphore - The sem_id of the semaphore.
   Returns      -   Returns 0 or -1 if the semaphore value is invalid.
   Side Effects -   
   ----------------------------------------------------------------------- */
int semv_real(int semaphore)
{
    int retValue = 0;
    user_ptr pop_ptr;

    // If the semaphore value is less than 0, return -1.
    if (semaphore < 0)
    {
        retValue = -1;
        return retValue;
    }

    // Increment the semValue and perform an mboxsend to block.
    SemTable[semaphore].semValue++;
    MboxSend(SemTable[semaphore].semMbox, NULL, 0);

    // If there are processes on the waitingprocs list.
    if (SemTable[semaphore].waitingProcs.count != 0)
    {
        // Pop the process off the list and call mboxsend for the process.
        pop_ptr = ListPop(&SemTable[semaphore].waitingProcs);
        MboxSend(pop_ptr->mboxStartup, NULL, 0);
    }

    // Call mboxreceive to unblock.
    MboxReceive(SemTable[semaphore].semMbox, NULL, 0);

    return retValue;
} /* semv_real */


/* ------------------------------------------------------------------------
   Name         -   terminate_real
   Purpose      -   Terminates the invoking process and all its children and 
                    synchronizes with its parent’s Wait system call. The 
                    child Processes are terminated by zap’ing them.
   Parameters   -   exit_code - The quit status of the terminating process.
   Returns      -   None
   Side Effects -   
   ----------------------------------------------------------------------- */
void terminate_real(int exit_code)
{
    user_ptr proc_ptr;
    user_ptr pop_ptr;

    // Pointer to the slot in the UserProcTable.
    proc_ptr = &UserProcTable[getpid()%MAXPROC];

    // If there are children on the list.
    if (proc_ptr->children.count != 0)
    {
        // While the children count isn't zero.
        while(proc_ptr->children.count != 0)
        {
            // Pop each process and zap them.
            pop_ptr = ListPop(&proc_ptr->children);
            zap(pop_ptr->pid);
        }

        // If the terminating process is on a parent's children list.
        if (UserProcTable[proc_ptr->ppid%MAXPROC].children.count != 0)
        {
            // Pop the process of the list, set the status to EMPTY and call quit.
            pop_ptr = ListPop(&UserProcTable[proc_ptr->ppid%MAXPROC].children);
            pop_ptr->status = EMPTY;
            quit(exit_code);
        }

        // Set the status of the process to empty and call quit.
        proc_ptr->status = EMPTY;
        quit(exit_code);
    }

    else
    {
        // Set the status of the process to empty and call quit.
        proc_ptr->status = EMPTY;
        quit(exit_code);
    }
} /* terminate_real */


/* ------------------------------------------------------------------------
   Name         -   nullsys3
   Purpose      -   Check for invalid syscalls.
   Parameters   -   pSysarg - The argument passed from syscall.
   Returns      -   None
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
   Returns      -   The index for the available semaphore or -2 if there
                    are no more slots available in the table.
   Side Effects -   
   ----------------------------------------------------------------------- */
int GetSemID()
{
    int retValue = -2;

    // Increment i as long as i is less than MAXSEMS
    for (int i = 0; i < MAXSEMS; i++)
    {
        // Search for an empty slot in the SemTable and return the ID. 
        if (SemTable[i].status == EMPTY)
        {
            SemTable[i].sem_id = semID;
            retValue = semID;
            semID++;
            return retValue;
        }
    }

    // Return -2 if there are no more empty slots.
    return retValue;
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

    // Set the next_ptr to NULL so we're only returning the popped process.
    popProcess->next_ptr = NULL;
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

    // Increase the count in the list.
    pList->count++;
} /* ListAdd */