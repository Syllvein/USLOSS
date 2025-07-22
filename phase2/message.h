#define DEBUG2 1

typedef struct mail_slot mail_slot;       // Used for the MailSlotTable
typedef struct mail_slot *slot_ptr;       // A mail slot pointer
typedef struct mailbox mail_box;          // Used for the MailBoxTable
typedef struct mailbox *mbox_ptr;         // A mailbox pointer
typedef struct mbox_proc mbox_proc;       // Used for the ProcTable
typedef struct mbox_proc *mbox_proc_ptr;  // A mailbox process pointer
typedef struct SlotList SlotList;         // Linked Lists

struct SlotList {
   struct   mail_slot *pHead;
   struct   mail_slot *pTail;
   int      count;
}; // Linked list with a head and tail pointer and a count of the items on the list.

struct mailbox {
   int         mbox_id;       // The ID of the current mailbox.
   int         messageSize;   // The size of the message for the current mailbox.
   int         status;        // Status of the current mailbox. Is it READY or EMPTY?
   int         slots;         // The number of slots for the current mailbox.
   SlotList    nextMboxList;  // The linked list for messages.
   SlotList    sendBlockList; // The block list for sending messages.
   SlotList    recvBlockList; // The block list for receiving messages.
   slot_ptr    mailSlot;      // Pointer to the mail slot.
}; // Mailbox structure

struct mail_slot {
   int               mbox_id;              // ID for the mailbox of this mail slot.
   int               proc_id;              // ID of the process for this mail slot.
   int               messageSize;          // Size of the message.
   int               status;               // Status of the mail slot. READY or EMPTY?
   unsigned char     message[MAX_MESSAGE]; // Buffer for the message of current mail slot.
   int               releaseMboxPid;       // PID of a process calling MBoxRelease
   int               mailSlot_id;          // ID for the mail slot.
   slot_ptr          pNextSlot;            // A mail slot pointer.
}; // Mail slot Structure.

struct mbox_proc {
   int         mbox_id;       // ID for the mailbox of this process.
   int         pid;           // ID of the process.
   int         messageSize;   // Size of the message.
   int         status;        // Status of the process.
   char*       pMessage;      // Pointer to the message.
   mbox_ptr    pNextMailBox;  // Mailbox pointer.
}; // Mailbox Process Structure

// Unsure about the purpose of psr_bits structure or psr_values union.
struct psr_bits {
   unsigned int cur_mode:1;
   unsigned int cur_int_enable:1;
   unsigned int prev_mode:1;
   unsigned int prev_int_enable:1;
   unsigned int unused:28;
};

union psr_values {
   struct psr_bits bits;
   unsigned int integer_part;
};

// Status values for error tracking and blocking processes
#define EMPTY              0
#define READY              1
#define NO_MAIL            12
#define FULL_MBOX          13
#define RELEASEMBOX_BLOCK  14
#define ZERO_SEND_BLOCK    15
#define FIFO               16