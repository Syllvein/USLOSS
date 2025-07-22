typedef struct UserProcess UserProcess;     // Used for the UserProcessTable.
typedef struct UserProcess *user_ptr;       // A userprocess pointer.
typedef struct Semaphore Semaphore;         // Used for the SemTable.
typedef struct Semaphore *sem_ptr;          // A semaphore pointer.

typedef struct List {
    UserProcess *pHead;     // Head pointer for the head of the list.
    UserProcess *pTail;     // Tail pointer for the tail of the list.
    int         count;      // Count tracks how many items are on the list.
} List;                     // List structure.

typedef struct UserProcess {
    char        name[MAXNAME];          // The name of the user process.
    int         (*startFunc)(char *);   // The value to start the user process.
    char        *arg;                   // Function argument pointer.
    int         pid;                    // Process ID.
    int         ppid;                   // Parent process ID.
    int         mboxStartup;            // The ID of the process' mailbox.
    int         status;                 // The process status (IN_USE or EMPTY).
    int         stackSize;              // Size of the stack for the process.
    int         priority;               // The process' priority.
    user_ptr    next_ptr;               // Process next pointer.
    List        children;               // List of children for the process.
} UserProcess;                          // UserProcess structure.

typedef struct Semaphore {
    int     semMbox;        // Semaphore mailbox for blocking and unblocking.
    int     sem_id;         // Semaphore ID.
    int     semValue;       // Semaphre value which will increment or decrement.
    int     status;         // Semaphore status (EMPTY or IN_USE).
    List    waitingProcs;   // List of waiting processes.
} Semaphore;                // Semaphore structure.

// UserProcess and Semaphore status
#define EMPTY       0
#define IN_USE      1