// ============================
// GraceOS Process Manager
// NT-style Process Objects
// ============================

#ifndef GRACEOS_PROC_H
#define GRACEOS_PROC_H

#include "../../lib/libc/int.h"
#include "context.h"
#include "../mm/sasy/handle.h"

/* ============================
   Type Definitions
   ============================ */

typedef uint16_t pid_t;
typedef uint16_t csid_t;     /* Command Session ID (job control) */
typedef uint16_t uid_t;
typedef uint16_t gid_t;

/* ============================
   Process Limits
   ============================ */

#define MAX_PROC            8192
#define PROC_NAME_MAX       32
#define PROC_MAX_SEGMENTS   16
#define PROC_MAX_FDS        64
#define PROC_STACK_SIZE     0x10000     /* 64KB default stack */
#define PROC_STACK_GUARD    0x1000      /* 4KB guard page */

/* ============================
   Special PIDs
   ============================ */

#define PID_INVALID         0
#define PID_KERNEL          0       /* Kernel "process" */
#define PID_INIT            1       /* Init process */

/* ============================
   Process States
   ============================ */

typedef enum proc_state {
    PROC_NEW,           /* Just created, not yet ready */
    PROC_READY,         /* Ready to run */
    PROC_RUNNING,       /* Currently executing */
    PROC_WAIT,          /* Waiting for child (waitpid) */
    PROC_BLOCKED,       /* Blocked on I/O or lock */
    PROC_ZOMBIE,        /* Exited, waiting for parent */
    PROC_DEAD           /* Fully terminated, can be reaped */
} proc_state_t;

/* ============================
   Process Flags
   ============================ */

#define PROC_FLAG_NONE      0x00000000
#define PROC_FLAG_KERNEL    0x00000001  /* Kernel thread */
#define PROC_FLAG_INIT      0x00000002  /* Init process */
#define PROC_FLAG_ELEVATED  0x00000004  /* Has elevated privileges (doas) */
#define PROC_FLAG_NOFORK    0x00000008  /* Cannot fork */
#define PROC_FLAG_TRACED    0x00000010  /* Being debugged */
#define PROC_FLAG_ORPHAN    0x00000020  /* Parent died, reparented to init */

/* ============================
   Capability Bits
   ============================ */

#define CAP_NONE            0x00000000
#define CAP_KILL            0x00000001  /* Can send signals to any process */
#define CAP_SETUID          0x00000002  /* Can change UID/GID */
#define CAP_CHROOT          0x00000004  /* Can change root */
#define CAP_MOUNT           0x00000008  /* Can mount filesystems */
#define CAP_RAWIO           0x00000010  /* Can access raw I/O ports */
#define CAP_ADMIN           0x00000020  /* Full admin (root) */
#define CAP_ALL             0xFFFFFFFF

/* ============================
   File Descriptor (Forward Decl)
   ============================ */

typedef enum file_type {
    FILE_TYPE_NONE       = 0,
    FILE_TYPE_PIPE_READ  = 1,
    FILE_TYPE_PIPE_WRITE = 2,
} file_type_t;

typedef struct file {
    file_type_t type;
    int         pipe_id;    /* Pipe index in kernel pipe table */
} file_t;

/* ============================
   IPC Port (Forward Decl)
   ============================ */

typedef struct ipc_port ipc_port_t;

/* ============================
   Wait Queue Entry
   ============================ */

typedef struct wait_entry {
    pid_t waiting_for;      /* PID we're waiting for (-1 = any child) */
    int* status_ptr;        /* Where to store exit status */
} wait_entry_t;

/* ============================
   Process Control Block (PCB)
   NT-style process object
   ============================ */

typedef struct process {
    /* === Identity === */
    pid_t pid;                      /* Process ID */
    csid_t csid;                    /* Command Session ID (job tree) */
    pid_t parent;                   /* Parent PID */
    char name[PROC_NAME_MAX];       /* Process name */
    
    /* === State === */
    proc_state_t state;             /* Current state */
    int valid;                      /* Security validation flag */
    uint32_t flags;                 /* Process flags */
    
    /* === Timing === */
    uint64_t start_time;            /* When process was created (ticks) */
    uint64_t cpu_time;              /* Total CPU time used (ticks) */
    uint64_t est_runtime;           /* Estimated burst time (SJF) */
    uint64_t wait_time;             /* Time spent waiting (aging) */
    uint64_t last_run;              /* Last time this process ran */
    uint64_t sleep_until;           /* Wake up at this tick count (0 = not sleeping) */
    
    /* === Exit Info === */
    int exit_code;                  /* Exit status */
    int exit_signal;                /* Signal that killed process (if any) */
    
    /* === Memory === */
    uint64_t pml4;                  /* Page table root physical address */
    seg_handle_t segments[PROC_MAX_SEGMENTS];   /* SASY segment handles */
    uint64_t brk;                   /* Program break (heap end) */
   uint64_t stack_base;            /* User stack base */
   uint64_t stack_size;            /* User stack size */
   uint64_t kernel_stack_base;     /* Kernel thread stack base (if kernel) */
   uint64_t kernel_stack_size;     /* Kernel thread stack size (if kernel) */
    
    /* === Files === */
    file_t* fds[PROC_MAX_FDS];      /* File descriptor table */
    
    /* === Credentials === */
    uid_t uid;                      /* User ID */
    uid_t euid;                     /* Effective User ID */
    gid_t gid;                      /* Group ID */
    gid_t egid;                     /* Effective Group ID */
    uint32_t caps;                  /* Capability bits (legacy) */
    uint32_t caps_effective;        /* spm: effective capability set */
    uint32_t caps_bounding;         /* spm: bounding capability set (ceiling) */
    
    /* === IPC === */
    ipc_port_t* ipc;                /* IPC port namespace */
    
    /* === Scheduling === */
    int base_priority;              /* Base scheduling priority */
    int current_priority;           /* Current priority (with aging) */
    int nice;                       /* Nice value (-20 to 19) */
    
    /* === CPU Context === */
    struct cpu_context ctx;         /* Saved CPU state */
    struct fpu_context fpu;         /* FPU/SSE state */
    
    /* === Wait Info === */
    wait_entry_t wait;              /* For waitpid() */
    
    /* === Linked List (for scheduler queues) === */
    struct process* next;
    struct process* prev;
    
} process_t;

/* ============================
   Current Process Pointer
   ============================ */

extern process_t* current;

/* ============================
   Process Manager API
   ============================ */

/* Initialize process subsystem */
void proc_init(void);

/* Exec subsystem */
int proc_exec(process_t* p, const char* path, char** argv);
void exec_init(void);

/* Create a new process */
process_t* proc_create(process_t* parent);

/* Set process name */
void proc_set_name(process_t* p, const char* name);

/* Destroy a process (free resources) */
void proc_destroy(process_t* p);

/* Find process by PID */
process_t* proc_find(pid_t pid);

/* Get process count */
uint32_t proc_count(void);

/* Get all processes in a CSID group */
uint32_t proc_get_csid_group(csid_t csid, process_t** out, uint32_t max);

/* Allocate PID */
pid_t proc_alloc_pid(void);

/* Allocate CSID */
csid_t proc_alloc_csid(void);

/* ============================
   Process State Transitions
   ============================ */

/* Mark process as ready */
void proc_set_ready(process_t* p);

/* Mark process as blocked */
void proc_set_blocked(process_t* p);

/* Mark process as zombie */
void proc_set_zombie(process_t* p, int exit_code);

/* ============================
   Process Exit/Termination
   ============================ */

/* Exit current process with code */
void proc_exit(int exit_code);

/* Terminate a process with a signal */
void proc_terminate(process_t* p, int signal);

/* Reap zombie children (called by init) */
void proc_reap_zombies(void);

/* ============================
   Process Tree Operations
   ============================ */

/* Get children of a process */
uint32_t proc_get_children(pid_t parent, process_t** out, uint32_t max);

/* Reparent children to init */
void proc_reparent_children(pid_t old_parent);

/* Wake processes whose sleep_until timer has expired */
void proc_check_sleepers(void);

/* ============================
   Debug / Inspection
   ============================ */

/* Print process info */
void proc_dump(process_t* p);

/* Print all processes (ps) */
void proc_dump_all(void);

/* Print process tree */
void proc_dump_tree(void);

#endif /* GRACEOS_PROC_H */
