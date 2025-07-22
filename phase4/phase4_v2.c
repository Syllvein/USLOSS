#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <usyscall.h>
#include <provided_prototypes.h>
#include <driver.h>

/* ------------------------- Prototypes ----------------------------------- */
int diskRead_real(int unit, int track, int first, int sectors, void *buffer);
int diskSize_real(int unit, int *sector, int *track, int *disk);
int diskWrite_real(int unit, int track, int first, int sectors, void *buffer);
int orderByWake(void *pStruct1, void *pStruct2);
int sleep_real(int seconds);
static int ClockDriver(char *);
static int DiskDriver(char *);
static void *ListPopNode(List *pList);
static void ListAddNode(List *pList, void *pStructToAdd);
static void ListAddNodeInOrder(List *pList, void *pStructToAdd);
static void ListInitialize(List *pList, int nextPrevOffset, int (*orderFunction)(void *pNode1, void *pNode2));
static void ListRemoveNode(List *pList, void *pStructToRemove);
static void sysCall4(sysargs *pSysarg);
void check_kernel_mode (char *procName);
void InitDriverTable();

/* -------------------------- Globals ------------------------------------- */
int num_tracks [DISK_UNITS];
static int diskpids[DISK_UNITS];
// Semaphore to synchronize drivers and start3.
static int running;
// Semaphores to signal disk drivers populated by queue.
static int diskSemaphore[DISK_UNITS];
static struct driver_proc Driver_Table[MAXPROC];
List sleepingList;

/* ------------------------------------------------------------------------
   Name         -   start3
   Purpose      -   
   Parameters   -   arg - Function argument.
   Returns      -   Returns 0
   Side Effects -   
   ----------------------------------------------------------------------- */
int start3(char *arg)
{
    char name[128];
    char termbuf[10];
    int	clockPID;
    int	pid;
    int retValue = 0;
    int	status;
    
    // Check if start3 is in kernel mode.
    check_kernel_mode ("start3");
    
    // Initialize sys_vec with the syscall function.
    sys_vec[SYS_SLEEP] = sysCall4;
    sys_vec[SYS_DISKREAD] = sysCall4;
    sys_vec[SYS_DISKSIZE] = sysCall4;
    sys_vec[SYS_DISKWRITE] = sysCall4;

    /* Initialize the phase 4 process table */
    InitDriverTable();
    ListInitialize(&sleepingList, 0, orderByWake);

    /*
        * Create clock device driver 
        * I am assuming a semaphore here for coordination.  A mailbox can
        * be used instead -- your choice.
        */
    running = semcreate_real(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);

    if (clockPID < 0)
    {
        console("start3(): Can't create clock driver\n");
        halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
        * will V the semaphore "running" once it is running.
     */

    semp_real(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */

    for (int i = 0; i < DISK_UNITS; i++)
    {
        sprintf(termbuf, "%d", i);
        sprintf(name, "DiskDriver%d", i);
        diskSemaphore[i] = semcreate_real(0);
        diskpids[i] = fork1(name, DiskDriver, termbuf, USLOSS_MIN_STACK, 2);
        if (diskpids[i] < 0)
        {
            console("start3(): Can't create disk driver %d\n", i);
            halt(1);
        }
    }

    semp_real(running);
    semp_real(running);


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case names.
     */
    pid = spawn_real("start4", start4, NULL,  8 * USLOSS_MIN_STACK, 3);
    pid = wait_real(&status);

    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver
    join(&status);  // Call join for the ClockDriver.

    for (int i = 0; i < DISK_UNITS; i++)
    {
        semfree_real(diskSemaphore[i]);  // Free the driver signaling semaphore.
        join(&status);              // Call join for the DiskDriver.
    }

    return retValue;
} /* start3 */


/* ------------------------------------------------------------------------
   Name         -   ClockDriver
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
static int ClockDriver(char *arg)
{
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semv_real(running);
    psr_set(psr_get() | PSR_CURRENT_INT);

    while(! is_zapped())
    {
        result = waitdevice(CLOCK_DEV, 0, &status);

        if (result != 0)
        {
            return 0;
        }
        
        // Compute the current time and wake up any processes
        // whose time has come.
        if (sleepingList.count != 0)
        {
            proc_ptr sleep_ptr;
            sleep_ptr = sleepingList.pHead;
            
            if (sys_clock() >= sleep_ptr->wake_time)
            {
                sleep_ptr = ListPopNode(&sleepingList);
                semv_real(sleep_ptr->sem_id);
            }
        }
    }

    return 0;
} /* ClockDriver */


/* ------------------------------------------------------------------------
   Name         -   DiskDriver
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
static int DiskDriver(char *arg)
{
    int result;
    int status;
    int tracks;
    int unit = atoi(arg);
    device_request my_request;
    proc_ptr current_req;

    psr_set(psr_get() | PSR_CURRENT_INT);

    //if (DEBUG4 && debugflag4)
    //   console("DiskDriver(%d): started\n", unit);

    /* Get the number of tracks for this disk */
    my_request.opr = DISK_TRACKS;
    my_request.reg1 = &tracks;

    result = device_output(DISK_DEV, unit, &my_request);

    if (result != DEV_OK)
    {
        console("DiskDriver %d: did not get DEV_OK on DISK_TRACKS call\n", unit);
        console("DiskDriver %d: is the file disk%d present???\n", unit, unit);
        halt(1);
    }

    waitdevice(DISK_DEV, unit, &status);
    //if (DEBUG4 && debugflag4)
    //   console("DiskDriver(%d): tracks = %d\n", unit, num_tracks[unit]);

    semv_real(running);

    while (! is_zapped())
    {
        if (semp_real(diskSemaphore[unit]) == 0)
        {
            
        }
        
        else
        {
        
        }
    }

    return 0;
} /* DiskDriver */


/* ------------------------------------------------------------------------
   Name         -   sleep_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int sleep_real(int seconds)
{
    int retValue = 0;
    int pid = getpid();
    proc_ptr sleep_ptr;

    sleep_ptr = &Driver_Table[pid%MAXPROC];
    sleep_ptr->wake_time = sys_clock() + (seconds * 1000000);
    sleep_ptr->pid = pid;

    ListAddNodeInOrder(&sleepingList, sleep_ptr);
    semp_real(sleep_ptr->sem_id);

    return retValue;
} /* sleep_real */


/* ------------------------------------------------------------------------
   Name         -   diskRead_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int diskRead_real(int unit, int track, int first, int sectors, void *buffer)
{
    int retValue = 0;

    return retValue;
} /* diskRead_real */


/* ------------------------------------------------------------------------
   Name         -   diskWrite_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int diskWrite_real(int unit, int track, int first, int sectors, void *buffer)
{
    int retValue = 0;

    return retValue;
} /* diskWrite_real */


/* ------------------------------------------------------------------------
   Name         -   diskSize_real
   Purpose      -   
   Parameters   -   
   Returns      -   
   Side Effects -   
   ----------------------------------------------------------------------- */
int diskSize_real(int unit, int *sector, int *track, int *disk)
{
    int retValue = 0;

    return retValue;
} /* diskSize_real */


/* ------------------------------------------------------------------------
   Name         -   sysCall4
   Purpose      -   Manages all the system calls for phase4.
   Parameters   -   *pSysarg - The argument passed to sysCall.
   Returns      -   None
   Side Effects -   
   ----------------------------------------------------------------------- */
static void sysCall4(sysargs *pSysarg)
{
    int disk;
    int result;
    int sector;
    int track;

    // Check the number and find the sysCall opcode that matches.
    switch (pSysarg->number)
    {
        // If pSysarg->number is 12.
        case SYS_SLEEP:
            result = sleep_real((int)pSysarg->arg1);
            pSysarg->arg4 = (void *) result;
            break;

        // If pSysarg->number is 13.
        case SYS_DISKREAD:
            result = diskRead_real ((int) pSysarg->arg5, (int) pSysarg->arg3, 
                                    (int) pSysarg->arg4, (int) pSysarg->arg2, 
                                    (void *) pSysarg->arg1);

            if (result < 0)
            {
                pSysarg->arg4 = (void *) result;
            }

            else
            {
                pSysarg->arg1 = (void *) result;
                pSysarg->arg4 = (void *) 0;
            }

            break;

        // If pSysarg->number is 14.
        case SYS_DISKWRITE:
            result = diskWrite_real ((int) pSysarg->arg5, (int) pSysarg->arg3, 
                                    (int) pSysarg->arg4, (int) pSysarg->arg2, 
                                    (void *) pSysarg->arg1);

            if (result < 0)
            {
                pSysarg->arg4 = (void *) result;
            }

            else
            {
                pSysarg->arg1 = (void *) result;
                pSysarg->arg4 = (void *) 0;
            }
            
            break;

        // If pSysarg->number is 15.
        case SYS_DISKSIZE:
            result = diskSize_real((int) pSysarg->arg1, &sector, &track, &disk);
            
            if (result < 0)
            {
                pSysarg->arg4 = (void *) result;
            }

            else
            {
                pSysarg->arg1 = (void *) sector;
                pSysarg->arg2 = (void *) track;
                pSysarg->arg3 = (void *) disk;
                pSysarg->arg4 = (void *) 0;
            }
            
            break;

        // For everything else call halt.
        default:
            console("Bad system call number! Halting...\n");
            halt(1);
    }

    // Switch to user mode.
    psr_set(psr_get() & ~PSR_CURRENT_MODE);
} /* sysCall4 */


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
   Name         -   ListInitialize
   Purpose      -   Initializes a List type.
   Parameters   -   nextPrevOffset - offset from beginning of structure to
                                   the next and previous pointers with
                                   the structure that makes up the nodes.
                    orderFunction - used for sorting the list
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
static void ListInitialize(List *pList, int nextPrevOffset,  
                           int (*orderFunction)(void *pNode1, void *pNode2))
{
    pList->pHead = pList->pTail = NULL;
    pList->count = 0;
    pList->OrderFunction = orderFunction;
    pList->offset = nextPrevOffset;
}


/* ------------------------------------------------------------------------
   Name         -   ListAddNode
   Purpose      -   Adds a node to the end of the list.
   Parameters   -   List *pList        -  pointer to the list
                    void *pStructToAdd -  pointer to the structure to add
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
static void ListAddNode(List *pList, void *pStructToAdd)
{
    ListNode *pTailNode;
    ListNode *pNodeToAdd;  // the next and prev pointers within proc to add
    int listOffset;

    listOffset = pList->offset;
    pNodeToAdd = (ListNode *)((unsigned char *)pStructToAdd + listOffset);
    pNodeToAdd->pNext = NULL;

    if (pList->pHead == NULL)
    {
        pList->pHead = pList->pTail = pStructToAdd;
        pNodeToAdd->pPrev = NULL;
    }

    else
    {
        // point to the list within proc_ptr
        pTailNode = (ListNode *)((unsigned char *)pList->pTail + listOffset);      
        pTailNode->pNext = pStructToAdd;
        pNodeToAdd->pPrev = pList->pTail;
        pNodeToAdd->pNext = NULL; 
        pList->pTail = pStructToAdd;
    }

    pList->count++;
}


/* ------------------------------------------------------------------------
   Name         -   ListAddNodeInOrder
   Purpose      -   Adds a node to the list based on the order function
   Parameters   -   List *pList - pointer to the list
                    void *pStructToAdd - pointer to the structure to add
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
static void ListAddNodeInOrder(List *pList, void *pStructToAdd)
{
    ListNode *pCurrentNode;
    ListNode *pNodeToAdd;  // the next and prev pointers within proc to add
    void *pCurrentStructure;
    int listOffset;
    int positionFound = 0;

    // must have an order function
    if (pList->OrderFunction == NULL)
    {
        return;
    }

    listOffset = pList->offset;
    pNodeToAdd = (ListNode *)((unsigned char *)pStructToAdd + listOffset);
    pNodeToAdd->pNext = NULL;

    if (pList->pHead == NULL)
    {
        pList->pHead = pList->pTail = pStructToAdd;
        pNodeToAdd->pPrev = NULL;
        pList->count++;
    }

    else
    {
        // start at the beginning
        pCurrentNode = (ListNode *)((unsigned char *)pList->pHead + listOffset);

        // traverse the list looking for the insertion place.
        while (pCurrentNode != NULL && !positionFound)
        { 
            // keep a pointer to the structure         
            pCurrentStructure = (unsigned char *)pCurrentNode-listOffset;

            // OrderFunction returns a value of <= 0 if this is the position to insert
            if ((pList->OrderFunction(pCurrentStructure, pStructToAdd) <= 0) ||
                (pCurrentNode == NULL))
            {
                positionFound = 1;
            }

            else
            {
                /* if we are not at the end of the list, then move to the next node */
                if (pCurrentNode->pNext != NULL)
                {
                    pCurrentNode = (ListNode *)((unsigned char *)pCurrentNode->pNext + listOffset);
                }

                else
                {
                    pCurrentNode = NULL;
                }
            }
        }

        if (pCurrentNode == NULL)
        {
            // add to the end of the list
            ListAddNode(pList, pStructToAdd);
        }

        else
        {         
            // insert BEFORE the current node
            pNodeToAdd->pNext = pCurrentStructure;
            pNodeToAdd->pPrev = pCurrentNode->pPrev;
            pCurrentNode->pPrev = pStructToAdd;

            // move the head pointer if needed
            if (pNodeToAdd->pPrev == NULL)
            {
                pList->pHead = pStructToAdd;
            }

            else
            {
                ListNode *pPrevNode;
                pPrevNode = (ListNode *)((unsigned char *)pNodeToAdd->pPrev + listOffset);
                pPrevNode->pNext = pStructToAdd;
            }

            pList->count++;
        }
    }
}


/* ------------------------------------------------------------------------
   Name         -   ListRemoveNode
   Purpose      -   Removes the specified node from the list.
   Parameters   -   List *pList - pointer to the list
                    void *pStructToRemove - pointer to the structure to remove
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
static void ListRemoveNode(List *pList, void *pStructToRemove)
{
    ListNode *pPrevNode=NULL;
    ListNode *pNextNode=NULL;
    ListNode *pNodeToRemove;  // the next and prev pointers within proc to add
    int listOffset;

    listOffset = pList->offset;
    if (pList->count > 0)
    {
        pNodeToRemove = (ListNode *)((unsigned char *)pStructToRemove + listOffset);

        // if this is not the head and
        // prev and next are NULL, then the node is not on the list
        if (pList->pHead == pStructToRemove || pNodeToRemove->pNext != NULL || 
            pNodeToRemove->pPrev != NULL)
        {
            if (pNodeToRemove->pPrev != NULL)
            {
                pPrevNode = (ListNode *)((unsigned char *)pNodeToRemove->pPrev + listOffset);      
            }

            if (pNodeToRemove->pNext  != NULL)
            {
                pNextNode = (ListNode *)((unsigned char *)pNodeToRemove->pNext + listOffset);
            }

            if (pPrevNode != NULL && pNextNode != NULL)
            {
                pPrevNode->pNext = pNodeToRemove->pNext;
                pNextNode->pPrev = pNodeToRemove->pPrev;
            }

            else
            {
                if (pPrevNode == NULL)
                {
                    /* replace the first node */
                    pList->pHead = pNodeToRemove->pNext; 
                
                    if (pList->pHead)
                    {
                        pNextNode->pPrev = NULL;
                    }
                }

                if (pNextNode == NULL)
                {
                    /* replace the tail */
                    pList->pTail = pNodeToRemove->pPrev;
                
                    if (pList->pTail)
                    {
                        pPrevNode->pNext = NULL;
                    }
                }
            }

            pList->count--;

            pNodeToRemove->pNext = NULL;
            pNodeToRemove->pPrev = NULL;
        }
    }
}


/* ------------------------------------------------------------------------
   Name         -   ListPopNode
   Purpose      -   Removes the first node from the list and returns a pointer 
                    to it.
   Parameters   -   List *pList - pointer to the list
   Returns      -   The function returns a pointer to the removed node.
   Side Effects -
   ----------------------------------------------------------------------- */
static void *ListPopNode(List *pList)
{
    void *pNode=NULL;   
    ListNode *pNodeToRemove;  // the next and prev pointers within proc to add
    int listOffset;

    listOffset = pList->offset;
    if (pList->count > 0)
    {
        pNodeToRemove = (ListNode *)((unsigned char *)pList->pHead + listOffset);

        pNode = pList->pHead;
        pList->pHead = pNodeToRemove->pNext;
        pList->count--;

        // clear prev and next
        pNodeToRemove->pNext = NULL;
        pNodeToRemove->pPrev = NULL;
    }
    
    return pNode;
} /* ListPopNode */


/* ------------------------------------------------------------------------
   Name         -   InitDriverTable
   Purpose      -   memsets each index of the Driver_Table and assigns an 
                    mbox_id.
   Parameters   -   None
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
void InitDriverTable()
{   
    // Clear junk values from the Driver_Table at index j.
    for (int i = 0; i < MAXPROC; i++)
    {
        memset(&Driver_Table[i], 0, sizeof(Driver_Table[i]));
        Driver_Table[i].sem_id = semcreate_real(0);
        Driver_Table[i].mbox_id = MboxCreate(0, 0);
    }
} /* InitTables */

int orderByWake(void *pStruct1, void *pStruct2)
{
    proc_ptr p1, p2;
    p1 = (proc_ptr *)pStruct1;
    p2 = (proc_ptr *)pStruct2;
    return p2->wake_time - p1->wake_time;
}