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
int diskReadWrite(int io, int unit, int track, int first, int sectors, void *buffer);
int diskRead_real(int unit, int track, int first, int sectors, void *buffer);
int diskSize_real(int unit, int *sector, int *track, int *disk);
int diskWrite_real(int unit, int track, int first, int sectors, void *buffer);
int orderByTrack(void *pStruct1, void *pStruct2);
int orderByWake(void *pStruct1, void *pStruct2);
int sleep_real(int seconds);
static int ClockDriver(char *);
static int DiskDriver(char *);
static void *ListGetNextNode(List *pList,  void *pCurrentStucture);
static void *ListPopNode(List *pList);
static void ListAddNode(List *pList, void *pStructToAdd);
static void ListAddNodeInOrder(List *pList, void *pStructToAdd);
static void ListInitialize(List *pList, int nextPrevOffset, int (*orderFunction)(void *pNode1, void *pNode2));
static void ListRemoveNode(List *pList, void *pStructToRemove);
static void sysCall4(sysargs *pSysarg);
void check_kernel_mode (char *procName);
void InitDriverTable();

/* -------------------------- Globals ------------------------------------- */
int upElevator = 1;                                 // Flag for traveling up or down a list.
static int diskpids[DISK_UNITS];                    // Disks to be zapped.
static int diskSemaphore[DISK_UNITS];               // Disk semaphores.
static int diskTracks[DISK_UNITS];                  // Total tracks per disk.
static int running;                                 // Semaphore for blocking.
static struct driver_proc Driver_Table[MAXPROC];    // Driver_table for processes.
List diskQueues[DISK_UNITS];                        // List of processes waiting to READ/WRITE.
List sleepingList;                                  // List of sleeping processes.


/* ------------------------------------------------------------------------
   Name         -   start3
   Purpose      -   Initializes Driver_table, lists, syscall, and forks
                    driver processes.
   Parameters   -   arg - Function argument.
   Returns      -   Returns 0
   Side Effects -   
   ----------------------------------------------------------------------- */
int start3(char *arg)
{
    char name[128];
    char termbuf[10];
    int	clockPID = 0;
    int offset = 0;
    int retValue = 0;
    int	status = 0;
    
    // Check if start3 is in kernel mode.
    check_kernel_mode ("start3");
    
    // Initialize sys_vec with the syscall function.
    sys_vec[SYS_SLEEP] = sysCall4;
    sys_vec[SYS_DISKREAD] = sysCall4;
    sys_vec[SYS_DISKSIZE] = sysCall4;
    sys_vec[SYS_DISKWRITE] = sysCall4;

    // Initialize Driver_Table and both lists.
    InitDriverTable();
    ListInitialize(&sleepingList, 0, orderByWake);
    offset = (void *) &Driver_Table[0].next_ptr - (void *) &Driver_Table[0];
    
    for (int i = 0; i < DISK_UNITS; i++)
    {
        ListInitialize(&diskQueues[i], offset, orderByTrack);
    }

    // Create a semaphore for synchronizing.
    running = semcreate_real(0);

    // Create the ClockDriver.
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);

    // Error checking if something went wrong.
    if (clockPID < 0)
    {
        console("start3(): Can't create clock driver\n");
        halt(1);
    }

    // Block till the ClockDriver starts.
    semp_real(running);

    // Create the disk driver processes.
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
    spawn_real("start4", start4, NULL,  8 * USLOSS_MIN_STACK, 3);
    wait_real(&status);

    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver
    join(&status);  // Call join for the ClockDriver.

    for (int i = 0; i < DISK_UNITS; i++)
    {
        semfree_real(diskSemaphore[i]);  // Free the driver signaling semaphore.
        join(&status);                  // Call join for the DiskDriver.
    }

    return retValue;
} /* start3 */


/* ------------------------------------------------------------------------
   Name         -   ClockDriver
   Purpose      -   Wake sleeping processes.
   Parameters   -   arg - Function argument.
   Returns      -   Zero
   Side Effects -   
   ----------------------------------------------------------------------- */
static int ClockDriver(char *arg)
{
    int result;
    int status;
    proc_ptr sleep_ptr;

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
        
        // Check if it's time to wake up a sleeping process.
        if (sleepingList.count != 0)
        {
            // Pointer to the head of the list of sleeping processes.
            sleep_ptr = sleepingList.pHead;
            
            // If it's time, pop the process off the list and semv the process.
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
   Purpose      -   Processing Read and write for disk processes.
   Parameters   -   arg - Function argument.
   Returns      -   Zero.
   Side Effects -   
   ----------------------------------------------------------------------- */
static int DiskDriver(char *arg)
{
    int status;
    int retValue = 0;
    int unit = atoi(arg);
    device_request my_request;
    proc_ptr current_req = NULL;

    // Get the number of tracks for this disk.
    my_request.opr = DISK_TRACKS;
    my_request.reg1 = &diskTracks[unit];

    if (device_output(DISK_DEV, unit, &my_request) != DEV_OK)
    {
        console("DiskDriver %d: did not get DEV_OK on DISK_TRACKS call\n", unit);
        console("DiskDriver %d: is the file disk%d present???\n", unit, unit);
        halt(1);
    }

    // Wait for the request and block.
    waitdevice(DISK_DEV, unit, &status);
    semv_real(running);

    // While we're not zapped.
    while (! is_zapped())
    {
        // Block till a request comes in.
        semp_real(diskSemaphore[unit]);
        
        // If zapped, return.
        if (is_zapped())
        {
        	return retValue;
        }
        
        // Add the first request back to the list and then remove it so
        // ListGetNextNode knows the next process to get from the list.
        if (current_req != NULL)
        {
            if (current_req->next_ptr == NULL && current_req->prev_ptr == NULL)
            {
                ListAddNodeInOrder(&diskQueues[unit], current_req);
                ListRemoveNode(&diskQueues[unit], current_req);
            }
        }

        // Get a request from the list and then remove it.
        current_req = ListGetNextNode(&diskQueues[unit], current_req);
        ListRemoveNode(&diskQueues[unit], current_req);

        // Move the track to the beginning.
        my_request.opr = DISK_SEEK;
        my_request.reg1 = (void *) current_req->track_start;

        if (device_output(DISK_DEV, unit, &my_request) != DEV_OK)
        {
            console("DiskDriver %d: did not get DEV_OK on DISK_TRACKS call\n", unit);
            console("DiskDriver %d: is the file disk%d present???\n", unit, unit);
            halt(1);
        }
        
        // Wait for the request.
        waitdevice(DISK_DEV, unit, &status);

        // Set the sector count to zero and assign current track.
        current_req->track_curr = current_req->track_start;
        current_req->sector_curr = current_req->sector_start;
        current_req->sector_count = 0;

        // While the count is less than the number of sectors.
        while(current_req->sector_count < current_req->num_sectors)
        {
            // If the current sector equals the track size.
            if (current_req->sector_curr == DISK_TRACK_SIZE)
            {
                // Increment the current track by 1, set the current sector to 0,
                // and move the track.
                current_req->track_curr++;
                current_req->sector_curr = 0;
                my_request.opr = DISK_SEEK;
                my_request.reg1 = (void*) current_req->track_curr;

                if (device_output(DISK_DEV, unit, &my_request) != DEV_OK)
                {
                    console("DiskDriver %d: did not get DEV_OK on DISK_TRACKS call\n", unit);
                    console("DiskDriver %d: is the file disk%d present???\n", unit, unit);
                    halt(1);
                }

                // Wait for the request.
                waitdevice(DISK_DEV, unit, &status);
            }
            
            // Assign DISK_READ or DISK_WRITE to the request as well as the
            // starting sector and the buffer which will be incremented by 512.
            my_request.opr = current_req->operation;
            my_request.reg1 = (void *) current_req->sector_curr;
            my_request.reg2 = (current_req->sector_count * DISK_SECTOR_SIZE) + 
                                current_req->disk_buf;

            if (device_output(DISK_DEV, unit, &my_request) != DEV_OK)
            {
                console("DiskDriver %d: did not get DEV_OK on DISK_TRACKS call\n", unit);
                console("DiskDriver %d: is the file disk%d present???\n", unit, unit);
                halt(1);
            }
            
            // Wait for the request.
            waitdevice(DISK_DEV, unit, &status);

            // Increment the count and the track.
            current_req->sector_count++;
            current_req->sector_curr++;
        }

        // Unblock.
        semv_real(current_req->sem_id);
    }

    return retValue;
} /* DiskDriver */


/* ------------------------------------------------------------------------
   Name         -   sleep_real
   Purpose      -   Putting a process to sleep.
   Parameters   -   seconds - The time in seconds the process will sleep.
   Returns      -   Zero
   Side Effects -   
   ----------------------------------------------------------------------- */
int sleep_real(int seconds)
{
    int retValue = 0;
    int pid = getpid();
    proc_ptr sleep_ptr;

    // Pointer to the process on the Driver_Table
    sleep_ptr = &Driver_Table[pid%MAXPROC];

    // Set the time the process will wake up in milliseconds and assign the pid.
    sleep_ptr->wake_time = sys_clock() + (seconds * 1000000);
    sleep_ptr->pid = pid;

    // Add the process to the sleeping list and block.
    ListAddNodeInOrder(&sleepingList, sleep_ptr);
    semp_real(sleep_ptr->sem_id);

    return retValue;
} /* sleep_real */


/* ------------------------------------------------------------------------
   Name         -   diskSize_real
   Purpose      -   Gets the size for sector, track, and tracks per disk.
   Parameters   -   unit - disk unit.
                    sector - Sector size.
                    track - Track size.
                    disk - Total tracks per disk.
   Returns      -   Zero if all is ok, -1 if the unit is outside parameter.
   Side Effects -   
   ----------------------------------------------------------------------- */
int diskSize_real(int unit, int *sector, int *track, int *disk)
{
    int retValue = 0;

    // Check if unit is within parameters.
    if (unit < 0 || unit > 1 )
    {
        retValue = -1;
        return retValue;
    }

    // Populate sector, track and disk values.
    *sector = DISK_SECTOR_SIZE;
    *track = DISK_TRACK_SIZE;
    *disk = diskTracks[unit];

    return retValue;
} /* diskSize_real */


/* ------------------------------------------------------------------------
   Name         -   diskReadWrite
   Purpose      -   Read and write to a disk.
   Parameters   -   io - DISK_READ or DISK_WRITE.
                    unit - Disk unit.
                    track - Starting track.
                    first - Starting sector.
                    sector - Total sectors.
                    buffer - Memory address to read/write to.
   Returns      -   Zero if all is ok or -1 if unit, first, or track are
                    outside of allowed parameters.
   Side Effects -   
   ----------------------------------------------------------------------- */
int diskReadWrite(int io, int unit, int track, int first, int sectors, void *buffer)
{
    int retValue = 0;
    proc_ptr disk_proc_ptr;

    // Check if unit is outside allowed parameters.
    if (unit < 0 || unit > 1)
    {
        retValue = -1;
        return retValue;
    }
    
    // Check if track is outside allowed parameters.
    if (track < 0 || track > diskTracks[unit])
    {
        retValue = -1;
        return retValue;
    }
    
    // Check if first (sector_start) is outside allowed parameters.
    if (first < 0 || first  > DISK_TRACK_SIZE)
    {
        retValue = -1;
        return retValue;
    }

    // Populate the disk driver process.
    disk_proc_ptr = &Driver_Table[getpid()%MAXPROC];
    disk_proc_ptr->pid = getpid();
    disk_proc_ptr->operation = io;
    disk_proc_ptr->track_start = track;
    disk_proc_ptr->sector_start = first;
    disk_proc_ptr->num_sectors = sectors;
    disk_proc_ptr->disk_buf = buffer;

    // Add the process to the list, unblock the diskdriver, and
    // block the process.
    ListAddNodeInOrder(&diskQueues[unit], disk_proc_ptr);
    semv_real(diskSemaphore[unit]);
    semp_real(disk_proc_ptr->sem_id);

    return retValue;
} /* diskReadWrite */


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
        case SYS_SLEEP: // Syscall for putting a process to sleep.
            result = sleep_real((int)pSysarg->arg1);
            pSysarg->arg4 = (void *) result;
            break;

        // If pSysarg->number is 13.
        case SYS_DISKREAD: // Syscall for DISK_READ for a disk driver.
            result = diskReadWrite (DISK_READ, (int) pSysarg->arg5, (int) pSysarg->arg3, 
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
        case SYS_DISKWRITE: // Syscall for DISK_READ for a disk driver.
            result = diskReadWrite (DISK_WRITE, (int) pSysarg->arg5, (int) pSysarg->arg3, 
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
        case SYS_DISKSIZE: // Syscall for assigning values to a disk process.
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

        // For anything else call halt.
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
    pList->pHead = pList->pTail = NULL;     // Set the head and tail pointers to NULL.
    pList->count = 0;                       // Set count to zero.
    pList->OrderFunction = orderFunction;   // Assign the orderFunction for the List.
    pList->offset = nextPrevOffset;         // Set the offset.
} /* ListInitialize */


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
} /* ListAddNode */


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
} /* ListAddNodeInOrder */


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
        }
    }
} /* ListRemoveNode */


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

    // Set the tail to NULL if the head is also NULL.
    if (pList->pHead == NULL)
    {
        pList->pTail = NULL;
    }
    
    return pNode;
} /* ListPopNode */


/* ------------------------------------------------------------------------
   Name         -   ListGetNextNode
   Purpose      -   Gets the next node releative to current node.  Returns
                    the first node if pCurrentStucture is NULL
   Parameters   -   List *pList - pointer to the list
                    void *pCurrentStucture - pointer to current struct
   Returns      -   The function returns a pointer to the next structure.
   Side Effects -   
   ----------------------------------------------------------------------- */
static void *ListGetNextNode(List *pList,  void *pCurrentStucture)
{
    void *pNode=NULL;   
    ListNode *pCurrentNode;  // Node part of the current structure
    int listOffset;

    if (pList != NULL)
    {
        if (pCurrentStucture == NULL)
        {
            pNode = pList->pHead;
        }

        else
        {
            listOffset = pList->offset;

            if (pList->count > 0)
            {
                pCurrentNode = (ListNode *)((unsigned char *)pCurrentStucture + listOffset);
                
                // Check if the upElevator flag is set.
                if (upElevator)
                {
                    // If the next pointer is NULL, switch direction and flip the
                    // upElevator flag off so the list starts going the other way.
                    if (pCurrentNode->pNext == NULL)
                    {
                        pNode = pCurrentNode->pPrev;
                        upElevator = 0;
                    }

                    else
                    {
                        pNode = pCurrentNode->pNext;
                    }
                }

                else
                {
                    // If the prev pointer is NULL, switch direction and flip the
                    // upElevator flag on so the list starts going the other way.
                    if (pCurrentNode->pPrev == NULL)
                    {
                        pNode = pCurrentNode->pNext;
                        upElevator = 1;
                    }

                    else
                    {
                        pNode = pCurrentNode->pPrev;
                    }
                }
            }
        }
    }

    return pNode;
} /* ListGetNextNode */



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
    // Clear junk values from the Driver_Table at index i.
    for (int i = 0; i < MAXPROC; i++)
    {
        memset(&Driver_Table[i], 0, sizeof(Driver_Table[i]));
        Driver_Table[i].disk_buf = NULL;
        Driver_Table[i].next_ptr = NULL;
        Driver_Table[i].prev_ptr = NULL;
        Driver_Table[i].sem_id = semcreate_real(0);
    }
} /* InitTables */


/* ------------------------------------------------------------------------
   Name         -   orderByWake
   Purpose      -   Sort the list by wake_time in ascending order.
   Parameters   -   pStruct1 - Void pointer.
                    pStruct2 - Void pointer.
   Returns      -   The difference between the pointer values so it knows
                    where to place each process on the list.
   Side Effects -   
   ----------------------------------------------------------------------- */
int orderByWake(void *pStruct1, void *pStruct2)
{
    proc_ptr p1, p2;
    p1 = (proc_ptr )pStruct1;
    p2 = (proc_ptr )pStruct2;
    return p2->wake_time - p1->wake_time;
} /* orderByWake */


/* ------------------------------------------------------------------------
   Name         -   orderByTrack
   Purpose      -   Sort the list by track_start in ascending order.
   Parameters   -   pStruct1 - Void pointer.
                    pStruct2 - Void pointer.
   Returns      -   The difference between the pointer values so it knows
                    where to place each process on the list.
   Side Effects -    
   ----------------------------------------------------------------------- */
int orderByTrack(void *pStruct1, void *pStruct2)
{
    proc_ptr p1, p2;
    p1 = (proc_ptr )pStruct1;
    p2 = (proc_ptr )pStruct2;

    // Sort by process ID if the two processes happen to have the same
    // track_start value, otherwise sort by track_start.
    if ((p2->track_start - p1->track_start) == 0)
    {
        return p2->pid - p1->pid;
    }

    else
    {
        return p2->track_start - p1->track_start;
    }
    
} /* orderByTrack */