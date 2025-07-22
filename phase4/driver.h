// Disk driver pointer.
typedef struct driver_proc * proc_ptr;

struct driver_proc {
    proc_ptr    next_ptr;       // Pointer to next process.
    proc_ptr    prev_ptr;       // Pointer to prev process.
    int         pid;            // Process ID.
    int         sem_id;         // Semaphore ID.
    int         wake_time;      // Track when a process meeds to wake up.
    int         operation;      // DISK_READ or DISK_WRITE.
    int         track_start;    // Track starting location.
    int         track_curr;     // Track current location.
    int         sector_start;   // Sector starting location.
    int         sector_count;   // Sector count.
    int         num_sectors;    // Total sectors.
    void        *disk_buf;      // Buffer location.
}; // Disk driver process structure.

typedef struct 
{
    void *pNext;    // Pointer to next node.
    void *pPrev;    // Pointer to previous node.
} ListNode; // ListNode structure

typedef struct 
{
    void *pHead;    // Pointer to the head of the list.
    void *pTail;    // Pointer to the tail of a list.
    int count;      // List count.
    int offset;     // offset of ListNode within the structure.
    int (*OrderFunction)(void *pNode1, void *pNode2);   // Function for sorting a list.
} List; // List structure.