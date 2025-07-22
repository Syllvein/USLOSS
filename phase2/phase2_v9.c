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
extern int start2 (char *);
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
static int GetNextMboxId();
static void clock_handler2(int dev, void *unit);
static void disk_handler(int dev, void *unit);
static void InitializeHandlers();
static void nullsys(sysargs *args);
static void syscall_handler(int dev, void *unit);
static void term_handler(int dev, void *unit);
struct mail_slot *ListPop(SlotList *pList);
void check_kernel_mode (char *procName);
void disableInterrupts ();
void enableInterrupts ();
void InitTables ();
void ListAdd(SlotList *pList, void* pNode);


/* -------------------------- Globals ------------------------------------- */
int debugflag2 = 0;

// The tables for our mailbox, mailslot, and process structures.
mail_box MailBoxTable[MAXMBOX];
mail_slot MailSlotTable[MAXSLOTS];
mbox_proc ProcTable[MAXPROC];

// Set numMboxes to zero for tracking how many mailboxes we have.
int numMboxes = 0;

// Count for our clock_handler so it performs a
// CondSend when it's called five times.
int clockInterruptCount = 0;

/* Device interrupt handlers */
int clockDevice;
int terminalDevice [4];
int diskDevice [2];

// Creating our lists.
SlotList slot_list;
SlotList send_proc_block;
SlotList recv_proc_block;
SlotList release_proc;

// Sys vec pointers.
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

    // Check if we're in kernel mode.
    check_kernel_mode("start1");

    /* Disable interrupts */
    disableInterrupts();

    // Initialize our Tables.
    InitTables();

    // Set sys_vec to point to nullsys.
    for (int i = 0; i < MAXSYSCALLS; i++)
    {
        sys_vec[i] = nullsys;
    }

    // Initialize the device handlers.
    InitializeHandlers();

    // Assign the first seven mailboxes to the seven device handlers.
    // One clock, four terminal, and two disk.
    clockDevice = MboxCreate(0, 0);
    terminalDevice [0] = MboxCreate(0,0);
    terminalDevice [1] = MboxCreate(0,0);
    terminalDevice [2] = MboxCreate(0,0);
    terminalDevice [3] = MboxCreate(0,0);
    diskDevice [0] = MboxCreate(0,0);
    diskDevice [1] = MboxCreate(0,0);

    // Enable interrupts
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

    // Check if we're in kernel mode.
    check_kernel_mode("MboxCreate");

    // Disable interrupts
    disableInterrupts();

    // Check if the slot_size is greater that MAX_MESSAGE or if it's less than zero.
    if (slot_size < 0 || slot_size > MAX_MESSAGE)
    {
        return -1;
    }

    // Check if slots is less than zero.
    if (slots < 0)
    {
        return -1;
    }

    newMboxId = GetNextMboxId();            // Get an ID for the current mailbox.
    pMbox = &MailBoxTable[newMboxId];       // Set a pointer to the mailbox table.
    pMbox->mbox_id = newMboxId;             // Assign the mbox ID.
    pMbox->messageSize = slot_size;         // Set the slot size.
    pMbox->slots = slots;                   // Set the number of lsots for the mailbox.
    pMbox->status = READY;                  // Set the status to READY.
    pMbox->nextMboxList = slot_list;        // Set a pointer to the slot_list.
    pMbox->recvBlockList = recv_proc_block; // Set a pointer to the receive list.
    pMbox->sendBlockList = send_proc_block; // Set a pointer to the send list.

    // Enable interrupts
    enableInterrupts();

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

    // Check if we're in kernel mode.
    check_kernel_mode("MboxSend");

    // Disable interrupts
    disableInterrupts();

    // Check if msg_size is too larger for what was listed in MboxCreate.
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

    pid = getpid();                              // Get a process ID.
    procIndex = pid%MAXPROC;                     // Start the ProcTable at 1.
    ProcTable[procIndex].pid = pid;              // Assign the proc ID.
    ProcTable[procIndex].mbox_id = mbox_id;      // Assign the mailbox ID.
    ProcTable[procIndex].status = READY;         // Set status to READY.
    ProcTable[procIndex].pMessage = msg_ptr;     // Point to the message.
    ProcTable[procIndex].messageSize = msg_size; // Set the message size. 

    pSlot = &MailSlotTable[slotIndex];      // Point to the MailSlotTable.
    pSlot->mbox_id = mbox_id;               // Assign the mailbox ID.
    pSlot->proc_id = pid;                   // Assign the process ID.
    pSlot->status = READY;                  // Set status to READY.
    pSlot->messageSize = msg_size;          // Set the message size.

    MailBoxTable[mbox_id].mailSlot = pSlot; // Point the mailslot pointer to pSlot.
    pMailbox = &MailBoxTable[mbox_id];      // Point to the MailBoxTable.

    // Check if it's a zero slot mailbox and that there is something blocked on receive.
    if (pMailbox->slots == 0 && pMailbox->recvBlockList.count != 0)
    {
        // pop the message off receive list and set the next pointer to NULL.
        pop_slot = ListPop(&pMailbox->recvBlockList);
        pop_slot->pNextSlot = NULL;

        // Copy the message to the buffer and make sure the message size is accurate.
        memcpy(pop_slot->message, msg_ptr, msg_size);
        pop_slot->messageSize = msg_size;

        // Add the mailslot to the slot_list for the mailbox and
        // unblock the process associated with the pid.
        ListAdd(&pMailbox->nextMboxList, pop_slot);
        unblock_proc(pop_slot->proc_id);
    }

    // Check if a zero slot mailbox and there is nothing blocked on receive.
    else if (pMailbox->slots == 0 && pMailbox->recvBlockList.count == 0)
    {
        // Copy the message to the pSlot buffer, add to send block list,
        // and block the process.
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&pMailbox->sendBlockList, pSlot);
        block_me(ZERO_SEND_BLOCK);

        // If there is something on the release list.
        if (release_proc.count != 0)
        {
            // Pop the message off release list and set the next pointer to NULL.
            pop_slot = ListPop(&release_proc);
            pop_slot->pNextSlot = NULL;

            // Add the message to the slot_list and set the return value to -3.
            ListAdd(&pMailbox->nextMboxList, pop_slot);
            retValue = -3;

            // If the process calling release was blocked and there are no more
            // items on the release list.
            if (pop_slot->releaseMboxPid != 0)
            {
                // Unblock the process that called MailBoxRelease.
                unblock_proc(pop_slot->releaseMboxPid);
            }
        }
    }

    // If there is something on the receive block list
    else if (pMailbox->recvBlockList.count != 0)
    {
        // Pop the message off the receive list and set nextslot to NULL.
        pop_slot = ListPop(&pMailbox->recvBlockList);
        pop_slot->pNextSlot = NULL;

        // Copy the message to the mailslot message buffer and update the message size.
        memcpy(pop_slot->message, msg_ptr, msg_size);
        pop_slot->messageSize = msg_size;

        // Add the message to the slot_list and unblock the process.
        ListAdd(&pMailbox->nextMboxList, pop_slot);
        unblock_proc(pop_slot->proc_id);
    }

    // Check if the mailbox is full.
    else if (pMailbox->slots > 0 && pMailbox->slots == pMailbox->nextMboxList.count)
    {
        // Copy the message to the mailslot message buffer, add it to the send
        // block list, and block the process.
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&pMailbox->sendBlockList, pSlot);
        block_me(FULL_MBOX);

        // If there are messages on the release list.
        if (release_proc.count != 0)
        {
            // Pop the message off release list and set the next pointer to NULL.
            pop_slot = ListPop(&release_proc);
            pop_slot->pNextSlot = NULL;

            // Add the message to the slot_list and set the return value to -3.
            ListAdd(&pMailbox->nextMboxList, pop_slot);
            retValue = -3;

            // If the process calling release was blocked and there are no more
            // items on the release list.
            if (pop_slot->releaseMboxPid != 0)
            {
                // Unblock the process that called MailBoxRelease.
                unblock_proc(pop_slot->releaseMboxPid);
            }
        }
    }

    // If nothing is being difficult or complicated.
    else
    {
        // Copy the message to the mailslot message buffer and add it to the slot_list.
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&pMailbox->nextMboxList, pSlot);
    }

    // Enable interrupts
    enableInterrupts();

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
    int retValue = 0;
    int pid;
    int procIndex;

    // Check if we're in kernel mode.
    check_kernel_mode("MboxReceive");

    // Disable interrupts
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

    pid = getpid();                              // Get a process ID.
    procIndex = pid%MAXPROC;                     // Start the ProcTable at 1 instead of 0.
    ProcTable[procIndex].pid = pid;              // Assign the proc ID.
    ProcTable[procIndex].mbox_id = mbox_id;      // Assign the mailbox ID.
    ProcTable[procIndex].status = READY;         // Set status to READY.
    ProcTable[procIndex].pMessage = msg_ptr;     // Point to the message.
    ProcTable[procIndex].messageSize = msg_size; // Set the message size.

    pMailbox = &MailBoxTable[mbox_id];           // Point to the MailBoxTable.

    // Check if the message size from the mailslot is greater than
    // the size of the receive buffer.
    if (pMailbox->mailSlot != NULL && pMailbox->mailSlot->messageSize > msg_size)
    {
        return -1;
    }

    // If a zero slot mailbox and sendBlockList ha something on it.
    if (pMailbox->slots == 0 && pMailbox->sendBlockList.count != 0)
    {
        // Pop off the send list and set next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;

        // Add the message to the slot_list and unblock the process.
        ListAdd(&pMailbox->nextMboxList, pop_ptr);
        unblock_proc(pop_ptr->proc_id);

        // Pop the now populated message off the slot_list and set next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;

        // Prepare to search the list for the current process' message by setting
        // the popped process' status to FIFO.
        pop_ptr->status = FIFO;

        // Iterate through the list till we find a matching pid.
        while (pop_ptr->proc_id != pid)
        {
            ListAdd(&pMailbox->nextMboxList, pop_ptr);
            pop_ptr = ListPop(&pMailbox->nextMboxList);
            pop_ptr->pNextSlot = NULL;

            // Set a break condition incase the send process and
            // receiving process are different.
            if (pop_ptr->status == FIFO)
            {
                break;
            }
        }

        // Ready a pointer to put the list back the way it started minus the popped
        // process.
        slot_ptr fix_list;

        // If the current wasn't already the correct process.
        if (pop_ptr->status != FIFO)
        {
            // Iterate through the list till we get back to the original pHead.
            while (pMailbox->nextMboxList.pHead->status != FIFO)
            {
                fix_list = ListPop(&pMailbox->nextMboxList);
                ListAdd(&pMailbox->nextMboxList, fix_list);
            }

            // Return the pHead status to READY so we never have more then one FIFO.
            if (fix_list != NULL)
            {
                pMailbox->nextMboxList.pHead->status = READY;
            }
        }

        // Return status back to READY, copy the message to the msg_ptr,
        // assign the return value, and clear the mailslot.
        pop_ptr->status = READY;
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
        memset(&MailSlotTable[pop_ptr->mailSlot_id], 0, sizeof(MailSlotTable[pop_ptr->mailSlot_id]));
    }

    // If nothing is on the slot_list and the mailslot is NULL.
    else if (pMailbox->nextMboxList.count == 0 || pMailbox->mailSlot == NULL)
    {
        // Add the process to the receive block list and block the process.
        ListAdd(&pMailbox->recvBlockList, &ProcTable[procIndex]);
        block_me(NO_MAIL);

        // Check if the message size from the mailslot is greater than
        // the size of the receive buffer.
        if (pMailbox->mailSlot != NULL && pMailbox->mailSlot->messageSize > msg_size)
        {
            return -1;
        }

        // If there is something on the release list.
        if (release_proc.count != 0)
        {
            // Pop the message off the release list and set the next pointer to NULL.
            pop_ptr = ListPop(&release_proc);
            pop_ptr->pNextSlot = NULL;

            // Add the message to the slot_list and set the return value to -3.
            ListAdd(&pMailbox->nextMboxList, pop_ptr);
            retValue = -3;

            // If the process calling release was blocked and there are no more
            // items on the release list.
            if (pop_ptr->releaseMboxPid != 0)
            {
                // Unblock the process that called MailBoxRelease.
                unblock_proc(pop_ptr->releaseMboxPid);
            }
        }

        // Once the process is unblocked and nothing is on release list.
        else
        {
            // Pop the message off the slot_list and set next pointer to NULL.
            pop_ptr = ListPop(&pMailbox->nextMboxList);
            pop_ptr->pNextSlot = NULL;

            // Prepare to search the list for the current process' message by setting
            // the popped process' status to FIFO.
            pop_ptr->status = FIFO;

            // Iterate through the list till we find a matching pid.
            while (pop_ptr->proc_id != pid)
            {
                ListAdd(&pMailbox->nextMboxList, pop_ptr);
                pop_ptr = ListPop(&pMailbox->nextMboxList);
                pop_ptr->pNextSlot = NULL;

                // Set a break condition incase the send process and
                // receiving process are different.
                if (pop_ptr->status == FIFO)
                {
                    break;
                }
            }

            // Ready a pointer to put the list back the way it started minus the popped
            // process.
            slot_ptr fix_list;

            // If the current wasn't already the correct process.
            if (pop_ptr->status != FIFO)
            {
                // Iterate through the list till we get back to the original pHead.
                while (pMailbox->nextMboxList.pHead->status != FIFO)
                {
                    fix_list = ListPop(&pMailbox->nextMboxList);
                    ListAdd(&pMailbox->nextMboxList, fix_list);
                }

                // Return the pHead status to READY so we never have more then one FIFO.
                if (fix_list != NULL)
                {
                    pMailbox->nextMboxList.pHead->status = READY;
                }
            }

            // Return status back to READY, copy the message to the msg_ptr,
            // assign the return value, and clear the mailslot.
            pop_ptr->status = READY;
            memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
            retValue = pop_ptr->messageSize;
            memset(&MailSlotTable[pop_ptr->mailSlot_id], 0, sizeof(MailSlotTable[pop_ptr->mailSlot_id]));
        }
    }

    // If there is something on the send list.
    else if (pMailbox->sendBlockList.count != 0)
    {
        // Pop the message off the send list and set the next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;

        // Add the message to the slot_list and unblock the process.
        ListAdd(&pMailbox->nextMboxList, pop_ptr);
        unblock_proc(pop_ptr->proc_id);

        // Pop the now populated message off the slot_list and set next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;

        // Prepare to search the list for the current process' message by setting
        // the popped process' status to FIFO.
        pop_ptr->status = FIFO;

        // Iterate through the list till we find a matching pid.
        while (pop_ptr->proc_id != pid)
        {
            ListAdd(&pMailbox->nextMboxList, pop_ptr);
            pop_ptr = ListPop(&pMailbox->nextMboxList);
            pop_ptr->pNextSlot = NULL;

            // Set a break condition incase the send process and
            // receiving process are different.
            if (pop_ptr->status == FIFO)
            {
                break;
            }
        }

        // Ready a pointer to put the list back the way it started minus the popped
        // process.
        slot_ptr fix_list;

        // If the current wasn't already the correct process.
        if (pop_ptr->status != FIFO)
        {
            // Iterate through the list till we get back to the original pHead.
            while (pMailbox->nextMboxList.pHead->status != FIFO)
            {
                fix_list = ListPop(&pMailbox->nextMboxList);
                ListAdd(&pMailbox->nextMboxList, fix_list);
            }

            // Return the pHead status to READY so we never have more then one FIFO.
            if (fix_list != NULL)
            {
                pMailbox->nextMboxList.pHead->status = READY;
            }
        }

        // Return status back to READY, copy the message to the msg_ptr,
        // assign the return value, and clear the mailslot.
        pop_ptr->status = READY;
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
        memset(&MailSlotTable[pop_ptr->mailSlot_id], 0, sizeof(MailSlotTable[pop_ptr->mailSlot_id]));
    }

    // If there is something on the slot_list.
    else if (pMailbox->nextMboxList.count != 0)
    {
        // Pop the message off the slot_list and set next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;

        // Copy the message to the msg_ptr, set the return value, and clear the mailslot.
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
        memset(&MailSlotTable[pop_ptr->mailSlot_id], 0, sizeof(MailSlotTable[pop_ptr->mailSlot_id]));
    }

    // Enable interrupts
    enableInterrupts();

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
    int pid;
    int procIndex;
    int retValue;
    slot_ptr pop_ptr;
    mbox_ptr pMailbox;

    // Check if we're in kernel mode.
    check_kernel_mode("MboxCondReceive");

    pid = getpid();                              // Get a process ID.
    procIndex = pid%MAXPROC;                     // Start the ProcTable at 1 instead of 0.
    ProcTable[procIndex].pid = pid;              // Assign the proc ID.
    ProcTable[procIndex].mbox_id = mbox_id;      // Assign the mailbox ID.
    ProcTable[procIndex].status = READY;         // Set status to READY.
    ProcTable[procIndex].pMessage = msg_ptr;     // Point to the message.
    ProcTable[procIndex].messageSize = msg_size; // Set the message size.
    
    pMailbox = &MailBoxTable[mbox_id];           // Point to the MailBoxTable.

    // Check if mailbox has been released.
    if (pMailbox->slots == 0 && pMailbox->sendBlockList.count == 0)
    {
        return -1;
    }

    // If there is something on the send list.
    if (pMailbox->sendBlockList.count != 0)
    {
        // Pop the process off the send block list and set the next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;

        // Copy the message to the mailslot buffer and update the message size.
        memcpy(pop_ptr->message, msg_ptr, msg_size);
        pop_ptr->messageSize = msg_size;

        // Add the process to the slot_list and unblock the process.
        ListAdd(&pMailbox->nextMboxList, pop_ptr);
        unblock_proc(pop_ptr->proc_id);
    }

    // If there is something on the slot_list.
    if (pMailbox->nextMboxList.count != 0)
    {
        // Pop the process off the slot_list and set the next pointer to NULL.
        pop_ptr = ListPop(&pMailbox->nextMboxList);
        pop_ptr->pNextSlot = NULL;

        // Copy the message to the msg_ptr, set the return value, and clear the mailslot.
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
        memset(&MailSlotTable[pop_ptr->mailSlot_id], 0, sizeof(MailSlotTable[pop_ptr->mailSlot_id]));
    }

    // Return -2 if the mailbox is empty or no message was sent.
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

    // Check if we're in kernel mode.
    check_kernel_mode("MboxCondSend");

    // Get a mailslot.
    slotIndex = GetSlot();

    // Return -2 if there are no more mailslots.
    if (slotIndex == -2)
    {
        return -2;
    }

    pid = getpid();                              // Get a process ID.
    procIndex = pid%MAXPROC;                     // Start the ProcTable at 1 instead of 0.
    ProcTable[procIndex].pid = pid;              // Assign the proc ID.
    ProcTable[procIndex].mbox_id = mbox_id;      // Assign the mailbox ID.
    ProcTable[procIndex].status = READY;         // Set status to READY.
    ProcTable[procIndex].pMessage = msg_ptr;     // Point to the message.
    ProcTable[procIndex].messageSize = msg_size; // Set the message size.

    pSlot = &MailSlotTable[slotIndex];      // Point to the MailSlotTable.
    pSlot->mbox_id = mbox_id;               // Assign the mailbox ID.
    pSlot->proc_id = pid;                   // Assign the process ID.
    pSlot->status = READY;                  // Set status to READY.
    pSlot->messageSize = msg_size;          // Set the message size.

    MailBoxTable[mbox_id].mailSlot = pSlot; // Point the mailslot pointer to pSlot.
    pMailbox = &MailBoxTable[mbox_id];      // Point to the MailBoxTable.

    // If we haven't switched to a new mailbox and the slots equal slot
    // list.count, then we have used all the slots and return -2.
    // if slots equals zero, then it's a zero slot mailbox and we ignore it.
    if (&pMailbox->nextMboxList != NULL)
    {
        if (pMailbox->slots == pMailbox->nextMboxList.count && pMailbox->slots != 0)
        {
            return -2;
        }
    }

    // Check if mailbox has been released or the mailbox is empty.
    if (pMailbox->status == EMPTY)
    {
        return -1;
    }

    // If something is on the receive list.
    if (pMailbox->recvBlockList.count != 0)
    {
        // Pop the process off the receive block list and set the next pointer to NULL.
        pop_slot = ListPop(&pMailbox->recvBlockList);
        pop_slot->pNextSlot = NULL;

        // Copy the message to the mailslot message buffer and update message size.
        memcpy(pop_slot->message, msg_ptr, msg_size);
        pop_slot->messageSize = msg_size;

        // Add the process to the slot_list and unblock the process.
        ListAdd(&pMailbox->nextMboxList, pop_slot);
        unblock_proc(pop_slot->proc_id);
    }

    // Copy the message to the mailslot message buffer and add the process to the slot_list.
    memcpy(pSlot->message, msg_ptr, msg_size);
    ListAdd(&pMailbox->nextMboxList, pSlot);

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
    slot_ptr pop_slot = NULL;

    // Check if we're in kernel mode.
    check_kernel_mode("MboxRelease");

    // Disable interrupts
    disableInterrupts();

    pDeleteMbox = &MailBoxTable[mbox_id];   // Point to the mailbox we're going to delete.
    pid = getpid();                         // Get a process ID.
    ProcTable[pid%MAXPROC].pid = pid;       // Assign the process ID.
    ProcTable[pid%MAXPROC].status = READY;  // Set the process to READY.

    // Check for a valid mailbox.
    if (pDeleteMbox == NULL || pDeleteMbox->status == EMPTY)
    {
        return -1;
    }

    // Set the mailbox status to empty to prepare it for deletion.
    pDeleteMbox->status = EMPTY;

    // Check if there are processes on the send_proc_block list.
    while (pDeleteMbox->sendBlockList.count != 0)
    {
        // Pop the process off the send list, set the next pointer to NULL, and
        // add the process to the release process list.
        pop_ptr = ListPop(&pDeleteMbox->sendBlockList);
        pop_ptr->pNextSlot = NULL;
        ListAdd(&release_proc, pop_ptr);

        // If we've pulled the last process off the list, then we get the pid for
        // the process running MailBoxRelease.
        if (pDeleteMbox->sendBlockList.count == 0)
        {
            pop_ptr->releaseMboxPid = pid;
        }

        // Unblock the process.
        unblock_proc(pop_ptr->proc_id);
    }

    // Check if there are processes on the recv_proc_block list.
    while (pDeleteMbox->recvBlockList.count != 0)
    {
        // Pop the process of the receive list, set the next pointer to NULL, and
        // add the process to the release process list.
        pop_slot = ListPop(&pDeleteMbox->recvBlockList);
        pop_slot->pNextSlot = NULL;
        ListAdd(&release_proc, pop_slot);

        // If we've pulled the last process off the list, then we get the pid for
        // the process running MailBoxRelease.
        if (pDeleteMbox->recvBlockList.count == 0)
        {
            pop_slot->releaseMboxPid = pid;
        }

        // Unblock the process.
        unblock_proc(pop_slot->proc_id);
    }

    // If there is something on the release list.
    if (release_proc.count != 0)
    {
        // Block the process calling MailBoxRelease, when it is unblocked later
        // delete the mailbox, and decriment the number of mailboxes.
        block_me(RELEASEMBOX_BLOCK);
        memset(&MailBoxTable[mbox_id], 0, sizeof(MailBoxTable[mbox_id]));
        numMboxes--;
    }

    else
    {
        // Else just delete the mailbox and decriment the number of mailboxes.
        memset(&MailBoxTable[mbox_id], 0, sizeof(MailBoxTable[mbox_id]));
        numMboxes--;
    }

    // Enable interrupts
    enableInterrupts();

    return 0;
}/* MboxRelease */


/* ------------------------------------------------------------------------
   Name         -   check_io
   Purpose      -   To check if there are any blocked processes on any of
                    the i/o mailboxes.
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
    for (int i = 0; i < 7; i++)
    {
        if (MailBoxTable[i].recvBlockList.count != 0 || 
            MailBoxTable[i].sendBlockList.count != 0)
        {
            value = 1;
        }
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
    // Check if not in kernel mode.
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

    // If the number of mailboxes is less than the maximum number of mailboxes.
    if (numMboxes < MAXMBOX)
    {
        // Increment the mbox ID while the numbeer of mailboxes is less than the
        // max and the status of the mailbox isn't empty.
        while (numMboxes < MAXMBOX && MailBoxTable[nextMboxId].status != EMPTY)
        {
            nextMboxId++;
        }

        newMboxId = nextMboxId; // Get the new mailbox ID.
        numMboxes++;            // Increment the number of mailboxes in use.

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
    // Clear junk values from the MailboxTable at index i.
    for (int i = 0; i < MAXMBOX; i++)
    {
        memset(&MailBoxTable[i], 0, sizeof(MailBoxTable[i]));
    }

    // Clear junk values from the MailSlotTable at index j and clear the message buffer.
    for (int j = 0; j < MAXSLOTS; j++)
    {
        memset(&MailSlotTable[j], 0, sizeof(MailSlotTable[j]));
        memset(&MailSlotTable[j].message, 0, sizeof(MAX_MESSAGE));
    }

    // Clear junk values from the ProcTable at index k.
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

        // If the head isn't NULL.
        if (pList->pHead != NULL)
        {
            // Move the head to the next pointer.
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
   Side Effects -   None
   ----------------------------------------------------------------------- */
static void InitializeHandlers()
{
    int_vec[CLOCK_DEV] = clock_handler2;    // Initialize the clock.
    int_vec[DISK_DEV] = disk_handler;       // Initialize the disk.
    int_vec[TERM_DEV] = term_handler;       // Initialize the terminal.
    int_vec[SYSCALL_INT] = syscall_handler; // Initialize the syscall.
} /* InitializeHandlers */


/* ------------------------------------------------------------------------
   Name         -   nullsys
   Purpose      -   Error handling for invalid system calls. 
   Parameters   -   None
   Returns      -   None
   Side Effects -   None
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
   Side Effects -   None
   ----------------------------------------------------------------------- */
int waitdevice(int type, int unit, int *status)
{
    int result = 0;

    /* Sanity checks */
    check_kernel_mode("waitdevice");
    disableInterrupts();

    switch (type)
    {
        case CLOCK_DEV : // Clock mailbox
            result = MboxReceive(clockDevice, status, sizeof(int));
            break;

        case DISK_DEV: // Disk mailboxes
            result = MboxReceive(diskDevice[unit], status, sizeof(int));
            break;

        case TERM_DEV: // Terminal mailboxes
            result = MboxReceive(terminalDevice[unit], status, sizeof(int));
            break;

        default: // Check if the device type is wrong.
            console("waitdevice(): bad type (%d). Halting...\n", type);
            halt(1);
    }

    // We were zap'd!
    if (result == -3)
    {
        // Enable interrupts
        enableInterrupts();

        return -1;
    }

    else
    {
        // Enable interrupts
        enableInterrupts();
        
        return 0;
    }
}


/* -----------------------------------------------------------------------
   Name         -   clock_handler2
   Purpose      -   Calls time slice for round robin and condiontionally
                    sends a message to the clock mailbox. 
   Parameters   -   dev - Device type.
                    *unit - Index of the clock mailbox in the MailBoxTable.
   Returns      -   None
   Side Effects -   None
   ----------------------------------------------------------------------- */
static void clock_handler2(int dev, void *unit)
{
    int unitToInt = (long int)unit;
    int status;

    // Check if we're in kernel mode.
    check_kernel_mode("clock_handler2");

    // Disable interrupts
    disableInterrupts();

    // If the device isn't the clock handler then halt.
    if (dev != CLOCK_DEV)
    {
        console("clock_handler2(): dev %d is wrong. Halting...\n", dev);
        halt(1);
    }

    // Increment the count so we know when to MboxCondSend.
    clockInterruptCount++;

    // Once the count reaches five, get the status from the device_input,
    // call MboxCondSend, and reset the count to zero.
    if (clockInterruptCount > 4)
    {
        device_input(CLOCK_DEV, unitToInt, &status);
        MboxCondSend(unitToInt, &status, sizeof(status));
        clockInterruptCount = 0;
    }

    // For phase1 and round robin.
    time_slice();

    // Enable interrupts
    enableInterrupts();

}/* clock_handler2 */


/* ------------------------------------------------------------------------
   Name         -   disk_handler
   Purpose      -   Checks that arguements are appropriate for the disk
                    device and call MboxCondSend to the disk mailbox. 
   Parameters   -   dev - Device type.
                    *unit - Index of the disk mailbox in the MailBoxTable. 
   Returns      -   None
   Side Effects -   None
   ----------------------------------------------------------------------- */
static void disk_handler(int dev, void *unit)
{
    int status;
    int unitToInt = (long int)unit;

    // Check if we're in kernel mode.
    check_kernel_mode("disk_handler");

    // Disable interrupts
    disableInterrupts();

    // If device isn't the disk handler or the index is wrong.
    if (dev != DISK_DEV || unitToInt != 0 || unitToInt != 1)
    {
        console("Wrong device or unit for disk_handler.\n");
        halt(1);
    }
    
    // Get the device status from device_input and call MboxCondSend to the disk mailbox.
    device_input(DISK_DEV, unitToInt, &status);
    MboxCondSend(diskDevice[unitToInt], &status, sizeof(status));

    // Enable interrupts
    enableInterrupts();

} /* disk_handler */


/* ------------------------------------------------------------------------
   Name         -   syscall_handler
   Purpose      -   Assist with system calls 
   Parameters   -   dev - Device type.
                    *unit - System argument
   Returns      -   None
   Side Effects -   None
   ----------------------------------------------------------------------- */
static void syscall_handler(int dev, void *unit)
{
    sysargs *sys_ptr;
    sys_ptr = (sysargs *) unit;     // Pointer to sysargs structure.

    // Check if we're in kernel mode.
    check_kernel_mode("syscall_handler");

    // Disable interrupts
    disableInterrupts();

    console("syscall_handler(): started, dev = %d, sys number = %d\n", dev, sys_ptr->number);

    // Check if dev is syscall handler.
    if (dev != SYSCALL_INT)
    {
        console("syscall_handler(): dev %d is wrong. Halting...\n", dev);
        halt(1);
    }

    // Check if the sys number is within range.
    if (sys_ptr->number >= MAXSYSCALLS || sys_ptr->number < 0)
    {
        console("syscall_handler(): sys number %d is wrong. Halting...\n", sys_ptr->number);
        halt(1);
    }

    // Point to a function within the sys_vec array.
    sys_vec[sys_ptr->number](sys_ptr);

    // Enable interrupts
    enableInterrupts();

}/* syscall_handler */


/* ------------------------------------------------------------------------
   Name         -   term_handler
   Purpose      -   Check if the arguments being passed are appropriate for
                    the terminal device and perform a MboxCondSend to 
                    terminal mailbox.
   Parameters   -   dev - Device type.
                    *unit - Index of the terminal mailbox in the MailBoxTable.
   Returns      -   None
   Side Effects -   None
   ----------------------------------------------------------------------- */
static void term_handler(int dev, void *unit)
{
    int status;
    int unitToInt = (long int)unit;

    // Check if we're in kernel mode.
    check_kernel_mode("term_handler");

    // Disable interrupts
    disableInterrupts();

    // Check if terminal device and the unitToInt is within range.
    if (dev != TERM_DEV || unitToInt < 0 || unitToInt > 3)
    {
        console("Wrong device or unit for term_handler.\n");
        halt(1);
    }

    // Get the device status from device_input and call MboxCondSend to terminal mailbox.
    device_input(TERM_DEV, unitToInt, &status);
    MboxCondSend(terminalDevice[unitToInt], &status, sizeof(status));

    // Enable interrupts
    enableInterrupts();

}/* term_handler */


/* ------------------------------------------------------------------------
   Name         -   GetSlot
   Purpose      -   Get the next mailboc slot.
   Parameters   -   None
   Returns      -   The index for the abailable mailbox slot or -2 if there
                    are no more slots available.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int GetSlot()
{
    // If mailslots are less than the maximum mailslots.
    for (int i = 0; i < MAXSLOTS; i++)
    {
        // Search for an empty mailslot within the MailSlotTable and assign the ID 
        // to the mailslot and return it.
        if (MailSlotTable[i].status == EMPTY)
        {
            MailSlotTable[i].mailSlot_id = i;
            return i;
        }
    }

    // Return -2 if there are no more mailslots.
    return -2;
}/* GetSlot */