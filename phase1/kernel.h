#define DEBUG 0

typedef struct proc_struct proc_struct;

typedef struct proc_struct * proc_ptr;

// Structure for managing linked lists of processes
typedef struct 
{
   struct proc_struct *pHead;
   struct proc_struct *pTail;
   int count;
} ProcList;

struct proc_struct {
   proc_ptr       next_proc_ptr;
   proc_ptr       child_proc_ptr;
   proc_ptr       next_sibling_ptr;
   proc_ptr       prev_sibling_ptr;
   proc_ptr       parent_proc_ptr;
   proc_ptr       next_zapped_ptr;
   proc_ptr       prev_zapped_ptr;
   char           name[MAXNAME];     /* process's name */
   char           start_arg[MAXARG]; /* args passed to process */
   context        state;             /* current context for process */
   short          pid;               /* process id */
   int            exitCode;
   int            priority;
   int (* start_func) (char *);   /* function where process begins -- launch */
   char          *stack;
   unsigned int   stacksize;
   int            status;         /* READY, BLOCKED, QUIT, etc. */
   /* other fields as needed... */
   int            kiddos;
   int            zapped;
   int            cpuTime;
};

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

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY LOWEST_PRIORITY

// Status define
#define EMPTY           0
#define READY           1
#define RUNNING         2
#define QUIT            3
#define JOIN_BLOCKED    4
#define ZAPPED          5
#define RECORDED_QUIT   6