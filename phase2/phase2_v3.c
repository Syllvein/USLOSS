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
int start1 (char *);
extern int start2 (char *);
static int GetNextMboxId();
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

int nextMboxId = 1;
int numMboxes = 0;

/* Device interrupt handlers */
int clockDevice;
int terminalDevice [4];
int diskDevice [2];

SlotList slot_list;
SlotList block_send_list;


/* -------------------------- Functions -----------------------------------
  Below I have code provided to you that calls

  check_kernel_mode
  enableInterrupts
  disableInterupts
  
  These functions need to be redefined in this phase 2,because
  their phase 1 definitions are static 
  and are not supposed to be used outside of phase 1.  */

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
    mbox_ptr pMbox;

    if (slot_size < 0 || slot_size > MAX_MESSAGE)
    {
        console("The slot_size is beyond the specified parameters.\n");
        return -1;
    }

    numMboxes++;
    newMboxId = GetNextMboxId();
    pMbox = &MailBoxTable[newMboxId%MAXMBOX];
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
   Returns      -   Zero if successful, -1 if invalid args.
   Side Effects -   None
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    int retValue = 0;
    int pid;
    int slotIndex;
    mbox_ptr pMailbox;
    slot_ptr pSlot = NULL;

    disableInterrupts();

    // Get an empty slot.
    for (int i = 0; i < MAXSLOTS; i++)
    {
        if (MailSlotTable[i%MAXSLOTS].status == EMPTY)
        {
            slotIndex = i;
            break;
        }
    }

    pid = getpid();
    ProcTable[pid%MAXPROC].pid = pid;
    ProcTable[pid%MAXPROC].mbox_id = mbox_id;
    ProcTable[pid%MAXPROC].status = READY;
    ProcTable[pid%MAXPROC].pMessage = msg_ptr;
    ProcTable[pid%MAXPROC].messageSize = msg_size;

    pSlot = &MailSlotTable[slotIndex];
    pSlot->mbox_id = mbox_id;
    pSlot->proc_id = pid;
    pSlot->status = READY;
    pSlot->messageSize = msg_size;

    MailBoxTable[mbox_id].mailSlot = pSlot;
    pMailbox = &MailBoxTable[mbox_id];

    if (pMailbox->recv_block_ptr != NULL)
    {
        memcpy(pMailbox->recv_block_ptr->pMessage, msg_ptr, msg_size);
        ListAdd(&slot_list, pSlot);
        pMailbox->recv_block_ptr->messageSize = msg_size;
        unblock_proc(pMailbox->recv_block_ptr->pid);
    }

    else if (pMailbox->slots == slot_list.count)
    {
        memcpy(pSlot->message, msg_ptr, msg_size);
        ListAdd(&block_send_list, pSlot);
        block_me(FULL_MBOX);
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
    mbox_ptr pMailbox;
    slot_ptr pop_ptr;
    pMailbox = &MailBoxTable[mbox_id];
    int retValue = 0;
    int pid;

    disableInterrupts();

    pid = getpid();
    ProcTable[pid%MAXPROC].pid = pid;
    ProcTable[pid%MAXPROC].status = READY;
    ProcTable[pid%MAXPROC].pMessage = msg_ptr;
    ProcTable[pid%MAXPROC].messageSize = msg_size;

    if (slot_list.count == 0)
    {
        pMailbox->recv_block_ptr = &ProcTable[pid%MAXPROC];
        block_me(NO_MAIL);

        if (pMailbox->recv_block_ptr != NULL)
        {
            retValue = pMailbox->recv_block_ptr->messageSize;
        }
    }

    else if (block_send_list.count != 0)
    {
        pop_ptr = ListPop(&block_send_list);
        pop_ptr->pNextSlot = NULL;
        ListAdd(&slot_list, pop_ptr);
        unblock_proc(pop_ptr->proc_id);

        if (pop_ptr != NULL)
        {
            pop_ptr = ListPop(&slot_list);
            memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
            retValue = pop_ptr->messageSize;
        }
    }

    else if (slot_list.count != 0)
    {
        pop_ptr = ListPop(&slot_list);
        memcpy(msg_ptr, pop_ptr->message, pop_ptr->messageSize);
        retValue = pop_ptr->messageSize;
    }

    

   return retValue;
} /* MboxReceive */


/* ------------------------------------------------------------------------
   Name         -   check_io
   Purpose      -  
   Parameters   -   None
   Returns      -   None
   Side Effects -   None
   ----------------------------------------------------------------------- */
int check_io()
{
    int value = 0;

    return 0;
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
    int newMboxId = -1;
    int mbox = nextMboxId % MAXMBOX;

    if (numMboxes <= MAXMBOX)
    {
        while (numMboxes < MAXMBOX && MailBoxTable[mbox].status != EMPTY)
        {
            nextMboxId++;
            mbox = nextMboxId % MAXMBOX;
        }

        newMboxId = nextMboxId++;
    }

    return newMboxId;
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

} /* DeleteProcess */


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
        while(pList->pTail->pNextSlot != NULL)
        {
            pList->pTail = pList->pTail->pNextSlot;
        }

        pList->pTail->pNextSlot = pNode;
        pList->pTail = pList->pTail->pNextSlot;
    }

    // Increase the count in the SlotList.
    pList->count++;
} /* ListAdd */