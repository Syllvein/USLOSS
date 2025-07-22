/* ------------------------------------------------------------------------
   phase2.c
   Applied Technology
   College of Applied Science and Technology
   The University of Arizona
   CSCV 452

   ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <string.h>

#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int check_io();
int GetSlot();
int MboxCondReceive(int mbox_id, void *msg_ptr, int msg_size);
int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size);
int MboxCreate(int slots, int slot_size);
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size);
int MboxRelease(int mbox_id);
int MboxSend(int mbox_id, void *msg_ptr, int msg_size);
int start1 (char *);
int waitdevice(int type, int unit, int *status);
extern int start2 (char *);
static int GetNextMboxId();
static void clock_handler2(int dev, void *unit);
static void disk_handler(int dev, void *unit);
static void InitializeHandlers();
static void nullsys(sysargs *args);
static void syscall_handler(int dev, void *unit);
static void term_handler(int dev, void *unit);
void check_kernel_mode (char *procName);
void enableInterrupts ();
void disableInterrupts ();
void InitTables ();
void ListAdd(SlotList *pList, void* pNode);
struct mail_slot *ListPop(SlotList *pList);


/* -------------------------- Globals ------------------------------------- */
int debugflag2 = 0;

/* the mail boxes */
mail_box MailBoxTable[MAXMBOX];
mail_slot MailSlotTable[MAXSLOTS];
mbox_proc ProcTable[MAXPROC];

int numMboxes = 0;
int clockInterruptCount = 0;

/* Device interrupt handlers */
int clockDevice;
int terminalDevice [4];
int diskDevice [2];

SlotList slot_list;
SlotList send_proc_block;
SlotList recv_proc_block;
SlotList release_proc;

void (*sys_vec[MAXSYSCALLS])(sysargs *args);

/* ------------------------------------------------------------------------
   Name         -   start1
   Purpose      -   Initializes mailboxes and interrupt vector.
                    Start the phase2 test process.
   Parameters   -   One, default arg passed by fork1, not used here.
   Returns      -   One to indicate normal quit.
   Side Effects -   Lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    int kid_pid, status; 

    if (DEBUG2 && debugflag2)
    {
        console("start1(): at beginning\n");
    }

    check_kernel_mode("start1");

    /* Disable interrupts */
    disableInterrupts();

    /* Initialize the mail box table, slots, & other data structures.
    * Initialize int_vec and sys_vec, allocate mailboxes for interrupt
    * handlers.  Etc... */
    InitTables();
    InitializeHandlers();

    for (int i = 0; i < MAXSYSCALLS; i++)
    {
        sys_vec[i] = nullsys;
    }


    /* Initialize seven mailboxes for device handlers. */
    /* One clock, four terminal, and two disk. */
    clockDevice = MboxCreate(0, 0);
    terminalDevice [0] = MboxCreate(0,0);
    terminalDevice [1] = MboxCreate(0,0);
    terminalDevice [2] = MboxCreate(0,0);
    terminalDevice [3] = MboxCreate(0,0);
    diskDevice [0] = MboxCreate(0,0);
    diskDevice [1] = MboxCreate(0,0);

    enableInterrupts();

    /* Create a process for start2, then block on a join until start2 quits */
    if (DEBUG2 && debugflag2)
    {
        console("start1(): fork'ing start2 process\n");
    }

    kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);

    if (join(&status) != kid_pid)
    {
        console("start2(): join returned something other than start2's pid\n");
    }

    return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name         -   MboxCreate
   Purpose      -   Gets a free mailbox from the table of mailboxes and 
                    initializes it 
   Parameters   -   Maximum number of slots in the mailbox and the max size
                    of a msg sent to the mailbox.
   Returns      -   -1 to indicate that no mailbox was created, or a value
                    >= 0 as the mailbox id.
   Side Effects -   Initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
    int newMboxId;
    mbox_ptr pMbox = NULL;

    if (slot_size < 0 || slot_size > MAX_MESSAGE)
    {
        console("The slot_size is beyond the specified parameters.\n");
        return -1;
    }

    newMboxId = GetNextMboxId();
    pMbox = &MailBoxTable[newMboxId];
    pMbox->mbox_id = newMboxId;
    pMbox->messageSize = slot_size;
    pMbox->slots = slots;
    pMbox->status = READY;

    return newMboxId;
} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name         -   MboxSend
   Purpose      -   Put a message into a slot for the indicated mailbox.
                    Block the sending process if no slot available.
   Parameters   -   mailbox id, pointer to data of msg, # of bytes in msg.
   Returns      -   Zero if successful, -1 if invalid args -3 if zapped or
                    mboxrelease on send_block.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    int retValue = 0;
    int pid;
    int slotIndex;
    int procIndex;
    mbox_ptr pMailbox = NULL;
    slot_ptr pSlot = NULL;
    slot_ptr pop_slot = NULL;

    disableInterrupts();

    // Check if msg_size is too larger for what was listed in
    // MboxCreate.
    if (msg_size > MailBoxTable[mbox_id].messageSize)
    {
        return -1;
    }

    // Check for an empty mailbox and return -1 if it is empty.
    if (MailBoxTable[mbox_id].status == EMPTY)
    {
        return -1;
    }

    // Get an empty slot.
    slotIndex = GetSlot();

    if (slotIndex == -2)
    {
        console("Out of slots!\n");
        halt(1);
    }

    pid = getpid();
    procIndex = pid%MAXPROC;
    ProcTable[procIndex].pid = pid;
    ProcTable[procIndex].mbox_id = mbox_id;
    ProcTable[procIndex].status = READY;
    ProcTable[procIndex].pMessage = msg_ptr;
    ProcTable[procIndex].messageSize = msg_size;

    pSlot = &MailSlotTable[slotIndex];
    pSlot->mbox_id = mbox_id;
    pSlot->proc_id = pid;
    pSlot->status = READY;
    pSlot->messageSize = msg_size;

    MailBoxTable[mbox_id].mailSlot = pSlot;
    pMailbox = &MailBoxTable[mbox_id];
    pMailbox->nextMboxList = &slot_list;
    pMailbox->recvBlockList = &recv_proc_block;
    pMailbox->sendBlockList = &send_proc_block;

    if (pMailbox->slots == 0 && recv_proc_block.count != 0)
    {
        pop_slot = ListPop(pMailbox->recvBlockList);
        pop_slot->pNextSlot = NULL;
        memcpy(pop_slot->message, msg_ptr, msg_size);
        pop_slot->messageSize = msg_size;
        ListAdd(&slot_list, pop_slot);
        unblock_proc(pop_slot->proc_id);
    }

    else if (pMailbox->slots == 0 && recv_proc_block.count == 0)
    {
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&send_proc_block, pSlot);
        block_me(ZERO_SEND_BLOCK);

        if (release_proc.count != 0)
        {
            pop_slot = ListPop(&release_proc);
            pop_slot->pNextSlot = NULL;
            ListAdd(&slot_list, pop_slot);
            retValue = -3;

            if (release_proc.count == 0)
            {
                unblock_proc(pop_slot->releaseMboxPid);
            }
        }
    }

    else if (recv_proc_block.count != 0)
    {
        pop_slot = ListPop(&recv_proc_block);
        pop_slot->pNextSlot = NULL;
        memcpy(pop_slot->message, msg_ptr, msg_size);
        pop_slot->messageSize = msg_size;
        ListAdd(&slot_list, pop_slot);
        unblock_proc(pop_slot->proc_id);
    }

    else if (pMailbox->slots > 0 && pMailbox->slots == slot_list.count)
    {
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&send_proc_block, pSlot);
        block_me(FULL_MBOX);

        if (release_proc.count != 0)
        {
            pop_slot = ListPop(&release_proc);
            pop_slot->pNextSlot = NULL;
            ListAdd(&slot_list, pop_slot);
            retValue = -3;

            if (release_proc.count == 0)
            {
                unblock_proc(pop_slot->releaseMboxPid);
            }
        }
    }

    else
    {
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&slot_list, pSlot);
    }

    return retValue;
} /* MboxSend */


/* ------------------------------------------------------------------------
   Name         -   MboxReceive
   Purpose      -   Get a msg from a slot of the indicated mailbox.
                    Block the receiving process if no msg available.
   Parameters   -   mailbox id, pointer to put data of msg, max # of bytes
                    that can be received.
   Returns      -   Actual size of msg if successful, -1 if invalid args.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    mbox_ptr pMailbox = NULL;
    slot_ptr pop_ptr = NULL;
    slot_ptr pSlot = NULL;
    int retValue = 0;
    int pid;
    int procIndex;

    disableInterrupts();

    // Check if the msg_size is somehow less than 0 or if it is greater
    // than the maximum message size.
    if (msg_size < 0 || msg_size > MAX_MESSAGE)
    {
        return -1;
    }

    // Check for an empty mailbox and return -1 if it is empty.
    if (MailBoxTable[mbox_id].status == EMPTY)
    {
        return -1;
    }

    pid = getpid();
    procIndex = pid%MAXPROC;
    ProcTable[procIndex].pid = pid;
    ProcTable[procIndex].mbox_id = mbox_id;
    ProcTable[procIndex].status = READY;
    ProcTable[procIndex].pMessage = msg_ptr;
    ProcTable[procIndex].messageSize = msg_size;

    pMailbox = &MailBoxTable[mbox_id];
    pMailbox->nextMboxList = &slot_list;
    pMailbox->recvBlockList = &recv_proc_block;
    pMailbox->sendBlockList = &send_proc_block;

    if (pMailbox->slots == 0 && send_proc_block.count != 0)
    {
        pop_ptr = ListPop(pMailbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;
        ListAdd(&slot_list, pop_ptr);
        unblock_proc(pop_ptr->proc_id);

        pop_ptr = ListPop(pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
    }

    else if (slot_list.count == 0 || pMailbox->mailSlot == NULL)
    {
        ListAdd(&recv_proc_block, &ProcTable[procIndex]);
        block_me(NO_MAIL);

        if (release_proc.count != 0)
        {
            pop_ptr = ListPop(&release_proc);
            pop_ptr->pNextSlot = NULL;
            ListAdd(&slot_list, pop_ptr);
            retValue = -3;

            if (release_proc.count == 0)
            {
                unblock_proc(pop_ptr->releaseMboxPid);
            }
        }

        else
        {
            pop_ptr = ListPop(pMailbox->nextMboxList);
            pop_ptr->pNextSlot = NULL;
            memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
            retValue = pop_ptr->messageSize;
        }
    }

    else if (send_proc_block.count != 0)
    {
        pop_ptr = ListPop(pMailbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;
        ListAdd(&slot_list, pop_ptr);
        unblock_proc(pop_ptr->proc_id);

        pop_ptr = ListPop(pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
    }

    else if (slot_list.count != 0)
    {
        pop_ptr = ListPop(pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
    }

   return retValue;
} /* MboxReceive */


/* ------------------------------------------------------------------------
   Name         -   MboxCondReceive
   Purpose      -   Conditionally receive a message from a mailbox without
                    blocking.
   Parameters   -   mailbox id, pointer to put data of msg, max # of bytes
                    that can be received.
   Returns      -   -3 if zapped, -2 if the mailbox is empty or if no
                    message was received, -1 if message is too large or
                    if illegal values are given as arguments. If all runs
                    without error, then it returns the size of the message.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int MboxCondReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    int slotIndex;
    int pid;
    int procIndex;
    int retValue;
    slot_ptr pSlot;
    slot_ptr pop_ptr;
    mbox_ptr pMailbox;

    slotIndex = GetSlot();

    pid = getpid();
    procIndex = pid%MAXPROC;

    ProcTable[procIndex].pid = pid;
    ProcTable[procIndex].mbox_id = mbox_id;
    ProcTable[procIndex].status = READY;
    ProcTable[procIndex].pMessage = msg_ptr;
    ProcTable[procIndex].messageSize = msg_size;

    pSlot = &MailSlotTable[slotIndex];
    pSlot->mbox_id = mbox_id;
    pSlot->proc_id = pid;
    pSlot->status = READY;
    pSlot->messageSize = msg_size;

    MailBoxTable[mbox_id].mailSlot = pSlot;
    pMailbox = &MailBoxTable[mbox_id];

    // Check if mailbox has been released.
    if (pMailbox->slots == 0 && send_proc_block.count == 0)
    {
        return -1;
    }

    pMailbox->nextMboxList = &slot_list;
    pMailbox->recvBlockList = &recv_proc_block;
    pMailbox->sendBlockList = &send_proc_block;

    if (send_proc_block.count != 0)
    {
        pop_ptr = ListPop(pMailbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;
        memcpy(pop_ptr->message, msg_ptr, msg_size);
        pop_ptr->messageSize = msg_size;
        ListAdd(&slot_list, pop_ptr);
        unblock_proc(pop_ptr->proc_id);
    }

    if (slot_list.count != 0)
    {
        pop_ptr = ListPop(pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
    }

    else
    {
        retValue = -2;
    }

    return retValue;
} /* MboxCondReceive */


/* ------------------------------------------------------------------------
   Name         -   MboxCondSend
   Purpose      -   Conditionally send a message to a mailbox without
                    blocking the process.
   Parameters   -   mailbox id, pointer to put data of msg, max # of bytes
                    that can be received.
   Returns      -   -3 if zapped, -2 if the mailbox is full or if there are
                    no slots available, -1 if illegal values are given as 
                    arguments, and 0 if the message is sent successfully.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size)
{
    int slotIndex;
    int pid;
    int procIndex;
    slot_ptr pSlot;
    slot_ptr pop_slot;
    mbox_ptr pMailbox;

    slotIndex = GetSlot();

    if (slotIndex == -2)
    {
        return -2;
    }

    pid = getpid();
    procIndex = pid%MAXPROC;

    ProcTable[procIndex].pid = pid;
    ProcTable[procIndex].mbox_id = mbox_id;
    ProcTable[procIndex].status = READY;
    ProcTable[procIndex].pMessage = msg_ptr;
    ProcTable[procIndex].messageSize = msg_size;

    pSlot = &MailSlotTable[slotIndex];
    pSlot->mbox_id = mbox_id;
    pSlot->proc_id = pid;
    pSlot->status = READY;
    pSlot->messageSize = msg_size;

    MailBoxTable[mbox_id].mailSlot = pSlot;
    pMailbox = &MailBoxTable[mbox_id];

    // If we haven't switched to a new mailbox and the slots equals slot
    // list.count, then we have used all the slots and return -2.
    // if slots equals zero, then it's a zero slot mailbox and we ignore it.
    if (pMailbox->nextMboxList != NULL)
    {
        if (pMailbox->slots == slot_list.count && pMailbox->slots != 0)
        {
            return -2;
        }
    }

    // Check if mailbox has been released.
    if (pMailbox->slots == 0 && recv_proc_block.count == 0)
    {
        return -1;
    }

    pMailbox->nextMboxList = &slot_list;
    pMailbox->recvBlockList = &recv_proc_block;
    pMailbox->sendBlockList = &send_proc_block;

    if (recv_proc_block.count != 0)
    {
        pop_slot = ListPop(pMailbox->recvBlockList);
        pop_slot->pNextSlot = NULL;
        memcpy(pop_slot->message, msg_ptr, msg_size);
        pop_slot->messageSize = msg_size;
        ListAdd(&slot_list, pop_slot);
        unblock_proc(pop_slot->proc_id);
    }

    memcpy(pSlot->message, msg_ptr, msg_size);
    ListAdd(&slot_list, pSlot);

    return 0;
}/* MboxCondSend */


/* ------------------------------------------------------------------------
   Name         -   MboxRelease
   Purpose      -   Release a mailbox that was previously created and
                    zapping any processes waiting on the mailbox.
   Parameters   -   mbox_id - ID of the mailbox to be released.
   Returns      -   -3 if zapped, -1 if the mailbox doesn't exist or is
                    empty, 0 if successful.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int MboxRelease(int mbox_id)
{
    int pid;
    mbox_ptr pDeleteMbox = NULL;
    slot_ptr pop_ptr = NULL;

    pDeleteMbox = &MailBoxTable[mbox_id];
    pid = getpid();
    ProcTable[pid%MAXPROC].pid = pid;
    ProcTable[pid%MAXPROC].status = READY;

    // Check for a valid mailbox.
    if (pDeleteMbox == NULL || pDeleteMbox->status == EMPTY)
    {
        return -1;
    }

    // Set the mailbox status to empty to prepare it for deletion.
    pDeleteMbox->status = EMPTY;

    // Check if there are processes on the send_proc_block list.
    while (send_proc_block.count != 0)
    {
        pop_ptr = ListPop(pDeleteMbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;
        ListAdd(&release_proc, pop_ptr);

        if (send_proc_block.count == 0)
        {
            pop_ptr->releaseMboxPid = pid;
        }

        unblock_proc(pop_ptr->proc_id);
    }

    // Check if there are processes on the recv_proc_block list.
    while (recv_proc_block.count != 0)
    {
        slot_ptr pop_slot;
        pop_slot = ListPop(pDeleteMbox->recvBlockList);
        pop_slot->pNextSlot = NULL;
        ListAdd(&release_proc, pop_slot);

        if (recv_proc_block.count == 0)
        {
            pop_slot->releaseMboxPid = pid;
        }

        unblock_proc(pop_slot->proc_id);
    }

    if (release_proc.count != 0)
    {
        block_me(RELEASEMBOX_BLOCK);
        memset(&MailBoxTable[mbox_id], 0, sizeof(MailBoxTable[mbox_id]));
        numMboxes--;
    }

    else
    {
        memset(&MailBoxTable[mbox_id], 0, sizeof(MailBoxTable[mbox_id]));
        numMboxes--;
    }

    return 0;
}/* MboxRelease */


/* ------------------------------------------------------------------------
   Name         -   check_io
   Purpose      -   To check if there are any blocked processes waiting for
                    i/o.
   Parameters   -   None
   Returns      -   value - If there is a blocked process value will be 1,
                    if there is not a blocked process, then value will be 0.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int check_io()
{
    int value = 0;

    // If either list has a count, which means it has a blocked process,
    // then return 1.
    if (send_proc_block.count != 0 || recv_proc_block.count != 0)
    {
        value = 1;
    }

    return value;
} /* check_io */


/* ------------------------------------------------------------------------
   Name         -   check_kernel_mode
   Purpose      -   To check if a process is in kernel mode or not.
   Parameters   -   procName - The name of the process to check.
   Returns      -   None
   Side Effects -   Halts if not in kernel mode.
   ----------------------------------------------------------------------- */
void check_kernel_mode (char *procName)
{
    if ((PSR_CURRENT_MODE & psr_get()) == 0)
    {
        console("Not in Kernel Mode! Halting...");
        halt(1);
    }
} /* check_kernel_mode */


/* ------------------------------------------------------------------------
   Name         -   enableInterrupts
   Purpose      -   Enable interrupts if in kernel mode.
   Parameters   -   None
   Returns      -   None
   Side Effects -   Halts if not in kernel mode.
   ----------------------------------------------------------------------- */
void enableInterrupts ()
{
    // Check if not in Kernel mode.
    if ((PSR_CURRENT_MODE & psr_get()) == 0)
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
} /* enableInterrupts */


/* ------------------------------------------------------------------------
   Name         -   disableInterrupts
   Purpose      -   Check if in kernel mode and disable interrupts if true.
   Parameters   -   None
   Returns      -   None
   Side Effects -   Halts if not in kernel mode.
   ----------------------------------------------------------------------- */
void disableInterrupts ()
{
    /* turn the interrupts OFF if we are in kernel mode */
    if ((PSR_CURRENT_MODE & psr_get()) == 0)
    {
        //not in kernel mode
        console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
        halt(1);
    } 

    else
    {
        /* We ARE in kernel mode */
        psr_set (psr_get() & ~PSR_CURRENT_INT);
    }
} /* disableInterrupts */


/* ------------------------------------------------------------------------
   Name         -   GetNextMboxId
   Purpose      -   The function returns a mailbox id for the new mailbox. 
   Parameters   -   None
   Returns      -   newMboxId
   Side Effects - 
   ------------------------------------------------------------------------ */
static int GetNextMboxId()
{
    int newMboxId = 0;
    int nextMboxId = 0;

    if (numMboxes < MAXMBOX)
    {
        while (numMboxes < MAXMBOX && MailBoxTable[nextMboxId].status != EMPTY)
        {
            nextMboxId++;
        }

        newMboxId = nextMboxId;
        numMboxes++;
        return newMboxId;
    }

    return -1;
} /* GetNextMboxId */


/* ------------------------------------------------------------------------
   Name         -   InitTables
   Purpose      -   memsets each index of a Table.
   Parameters   -   None
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
void InitTables()
{
    // Delete the location in the ProcTable indexed by the pid.
    for (int i = 0; i < MAXMBOX; i++)
    {
        memset(&MailBoxTable[i], 0, sizeof(MailBoxTable[i]));
    }

    for (int j = 0; j < MAXSLOTS; j++)
    {
        memset(&MailSlotTable[j], 0, sizeof(MailSlotTable[j]));
        memset(&MailSlotTable[j].message, 0, sizeof(MAX_MESSAGE));
    }

    for (int k = 0; k < MAXPROC; k++)
    {
        memset(&ProcTable[k], 0, sizeof(ProcTable[k]));
    }

} /* InitTables */


/* ------------------------------------------------------------------------
   Name         -   ListPop
   Purpose      -   Removes the first node in the list
   Parameters   -   pList - The list to add to
   Returns      -   Pointer to the first node or NULL if list is empty
   Side Effects - 
   ----------------------------------------------------------------------- */
struct mail_slot *ListPop(SlotList *pList)
{
   struct mail_slot *popProcess = NULL;

    // If there is a process to pop
    if (pList->count > 0)
    {
        // Pop the head of the list.
        popProcess = pList->pHead;

        if (pList->pHead != NULL)
        {
            pList->pHead = pList->pHead->pNextSlot;
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
void ListAdd(SlotList *pList, void* pNode)
{

    if (pList->count == 0)
    {
        // if the list is empty, set head and tail pointers to the only node
        pList->pHead = pList->pTail = pNode;
    }

    else
    {
        pList->pTail->pNextSlot = pNode;
        pList->pTail = pList->pTail->pNextSlot;
    }

    // Increase the count in the SlotList.
    pList->count++;
} /* ListAdd */


/* ------------------------------------------------------------------------
   Name         -   InitializeHandlers
   Purpose      -   Initializes the clock, disk, terminal, mmu, and syscall
                    handlers. 
   Parameters   -   None
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
static void InitializeHandlers()
{
    int_vec[CLOCK_DEV] = clock_handler2;
    int_vec[DISK_DEV] = disk_handler;
    int_vec[TERM_DEV] = term_handler;
    int_vec[SYSCALL_INT] = syscall_handler;
} /* InitializeHandlers */


/* ------------------------------------------------------------------------
   Name         -   nullsys
   Purpose      -   Error handling for invalid system calls. 
   Parameters   -   None
   Returns      -   None
   Side Effects - 
   ----------------------------------------------------------------------- */
static void nullsys(sysargs *args)
{
    console("nullsys(): Invalid syscall %d. Halting...\n", args->number);
    halt(1);
} /* nullsys */


/* ------------------------------------------------------------------------
   Name         -   waitdevice
   Purpose      -   Device waits until a message is received. 
   Parameters   -   None
   Returns      -   Returns -1 if zapped. Returns 0 if all goes well.
   Side Effects - 
   ----------------------------------------------------------------------- */
int waitdevice(int type, int unit, int *status)
{
    int result = 0;

    /* Sanity checks */
    check_kernel_mode("waitdevice");
    disableInterrupts();

    switch (type)
    {
        case CLOCK_DEV :
            result = MboxReceive(clockDevice, status, sizeof(int));
            break;
        case DISK_DEV:
            result = MboxReceive(diskDevice[unit], status, sizeof(int));
            break;
        case TERM_DEV:
            result = MboxReceive(terminalDevice[unit], status, sizeof(int));
            break;
        default:
            console("waitdevice(): bad type (%d). Halting...\n", type);
            halt(1);
    }

    if (result == -3)
    {
        /* we were zap’d! */
        return -1;
    }

    else
    {
        return 0;
    }
}


/* -----------------------------------------------------------------------
   Name         -   clock_handler2
   Purpose      -    
   Parameters   -   
   Returns      -   
   Side Effects - 
   ----------------------------------------------------------------------- */
static void clock_handler2(int dev, void *unit)
{
    int unitToInt = (int)unit;
    int status;
    int result;

    if (dev != CLOCK_DEV)
    {
        console("Not the clock_handler.\n");
        halt(1);
    }

    clockInterruptCount++;

    if (clockInterruptCount > 4)
    {
        device_input(CLOCK_DEV, unitToInt, &status);
        result = MboxCondSend(unitToInt, &status, sizeof(status));
        clockInterruptCount = 0;
    }

    time_slice();
}/* clock_handler2 */


/* ------------------------------------------------------------------------
   Name         -   disk_handler
   Purpose      -    
   Parameters   -   
   Returns      -   
   Side Effects - 
   ----------------------------------------------------------------------- */
static void disk_handler(int dev, void *unit)
{
    int status;
    int result;
    int unitToInt = (int)unit;

    if (dev != DISK_DEV || unitToInt != 0 || unitToInt != 1)
    {
        console("Wrong device or unit for disk_handler.\n");
        halt(1);
    }

    device_input(DISK_DEV, unitToInt, &status);
    result = MboxCondSend(diskDevice[unitToInt], &status, sizeof(status));
} /* disk_handler */


/* ------------------------------------------------------------------------
   Name         -   syscall_handler
   Purpose      -    
   Parameters   -   
   Returns      -   
   Side Effects - 
   ----------------------------------------------------------------------- */
static void syscall_handler(int dev, void *unit)
{
    sysargs *sys_ptr;
    sys_ptr = (sysargs *) unit;

    if (dev != SYSCALL_INT)
    {
        console("Interrupt is not SYSCALL_INT.\n");
        halt(1);
    }

    if (sys_ptr->number > MAXSYSCALLS)
    {
        console("Sys call is out of range.\n");
        halt(1);
    }

    sys_vec[sys_ptr->number](sys_ptr);
}/* syscall_handler */


/* ------------------------------------------------------------------------
   Name         -   term_handler
   Purpose      -    
   Parameters   -   
   Returns      -   
   Side Effects - 
   ----------------------------------------------------------------------- */
static void term_handler(int dev, void *unit)
{
    int status;
    int result;
    int unitToInt = (int)unit;

    if (dev != TERM_DEV || unitToInt < 0 || unitToInt > 3)
    {
        console("Wrong device or unit for term_handler.\n");
        halt(1);
    }

    device_input(TERM_DEV, unitToInt, &status);
    result = MboxCondSend(terminalDevice[unitToInt], &status, sizeof(status));
}/* term_handler */


/* ------------------------------------------------------------------------
   Name         -   GetSlot
   Purpose      -    
   Parameters   -   
   Returns      -   
   Side Effects - 
   ----------------------------------------------------------------------- */
int GetSlot()
{
    for (int i = 0; i < MAXSLOTS; i++)
    {
        if (MailSlotTable[i%MAXSLOTS].status == EMPTY)
        {
            return i%MAXSLOTS;
        }
    }

    return -2;
}/* GetSlot */