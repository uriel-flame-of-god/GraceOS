# GraceOS Process Manager

**NT/MS-DOS 4 Inspired Executive Subsystem**

## Overview

The Process Manager (PMGR) is GraceOS's process subsystem that provides:

- **True multitasking** - Preemptive scheduling with context switching
- **NT-style process objects** - Full Process Control Block (PCB)
- **Hybrid scheduler** - SJF + FCFS + Aging algorithm
- **CSID job trees** - Command Session ID for job control
- **Zombie handling** - Proper process exit and cleanup
- **Security validation** - Capability-based security
- **SASY integration** - Segment-based memory management
- **doas enforcement** - Privilege elevation control

## Executive Architecture

```
Kernel Executive
├── Memory Manager (PMM/VMM/SASY)
├── Security Policy Manager (SPM)  ← Initialised before PMGR
├── Process Manager (PMGR)         ← This
├── minit Orchestration Layer       ← Sits above PMGR
├── Scheduler
├── IPC Manager
├── Object Manager
└── Security Manager
```

PMGR owns all process objects and coordinates with the scheduler.
SPM validates process security before PMGR acts.
minit builds a named-service tree on top of PMGR.

## Directory Structure

```
kernel/proc/
├── proc.h       # Process structures and API
├── proc.c       # Process creation/destruction
├── sched.h      # Scheduler interface
├── sched.c      # Hybrid scheduler implementation
├── context.h    # CPU context structure
├── context.c    # Context initialization helpers
├── context.S    # Assembly context switch
├── table.c      # Process table management
├── exec.c       # Program loading (ELF)
├── fork.c       # Process cloning
├── wait.c       # Wait/waitpid implementation
├── zombie.c     # Exit and cleanup
├── job.c        # Job control (CSID)
├── security.c   # Security and doas
└── minit/
    ├── minit.h          # minit public API and node structure
    ├── tree.c           # Service tree: register, find, link
    ├── exhaustion.c     # Crash handling + restart exhaustion
    └── orchestration.c  # Spawn + kernel trampoline + stack pool
```

## Process States

| State | Description |
|-------|-------------|
| `PROC_NEW` | Just created, not yet ready |
| `PROC_READY` | Ready to run, in scheduler queue |
| `PROC_RUNNING` | Currently executing on CPU |
| `PROC_WAIT` | Waiting for child (waitpid) |
| `PROC_BLOCKED` | Blocked on I/O or lock |
| `PROC_ZOMBIE` | Exited, waiting for parent |
| `PROC_DEAD` | Fully terminated |

## Process Control Block (PCB)

```c
typedef struct process {
    /* Identity */
    pid_t pid;                  /* Process ID */
    csid_t csid;                /* Command Session ID */
    pid_t parent;               /* Parent PID */
    char name[32];              /* Process name */
    
    /* State */
    proc_state_t state;         /* Current state */
    int valid;                  /* Security validation flag */
    uint32_t flags;             /* Process flags */
    
    /* Timing */
    uint64_t start_time;        /* Creation time */
    uint64_t cpu_time;          /* Total CPU time */
    uint64_t est_runtime;       /* Estimated burst (SJF) */
    uint64_t wait_time;         /* Wait time (aging) */
    
    /* Memory */
    uint64_t pml4;              /* Page table root */
    seg_handle_t segments[16];  /* SASY segment handles */
    
    /* Files */
    file_t* fds[64];            /* File descriptor table */
    
    /* Credentials */
    uid_t uid, euid;            /* User IDs */
    gid_t gid, egid;            /* Group IDs */
    uint32_t caps;              /* Capabilities */
    
    /* Scheduling */
    int base_priority;          /* Base priority */
    int nice;                   /* Nice value */
    
    /* CPU Context */
    struct cpu_context ctx;     /* Saved registers */
    struct fpu_context fpu;     /* FPU/SSE state */
} process_t;
```

## Hybrid Scheduler Algorithm

The scheduler uses a hybrid approach combining:

### 1. Shortest Job First (SJF)
Processes with shorter estimated runtime get priority:
```c
score = est_runtime;
```

### 2. First Come First Served (FCFS) with Aging
Wait time reduces score, preventing starvation:
```c
score -= (wait_time / AGING_FACTOR);
```

### 3. Security Penalty
Invalid processes get a large penalty:
```c
if (!valid) score += INVALID_PENALTY;
```

### Score Formula
```
score = est_runtime - (wait_time / 100) + invalid_penalty
```

Lower score = higher priority.

## Context Switching

### x86_64 Context Structure
```c
struct cpu_context {
    uint64_t rip;       /* Instruction pointer */
    uint64_t rsp;       /* Stack pointer */
    uint64_t rflags;    /* Flags register */
    
    /* Callee-saved registers */
    uint64_t rbx, rbp;
    uint64_t r12, r13, r14, r15;
    
    /* Segments */
    uint64_t cs, ss;
    uint64_t cr3;       /* Page table */
};
```

### Assembly Context Switch
```nasm
ctx_switch:
    ; Save old context to [rdi]
    mov [rdi + 0x00], rax    ; Save RIP
    mov [rdi + 0x08], rsp    ; Save RSP
    ; ... save other registers
    
    ; Restore new context from [rsi]
    mov rbx, [rsi + 0x18]
    mov rbp, [rsi + 0x20]
    ; ... restore other registers
    
    mov rsp, [rsi + 0x08]    ; New stack
    jmp qword [rsi + 0x00]   ; Jump to new RIP
```

## Job Control (CSID)

Processes are grouped into Command Sessions:

- Each session has a unique CSID
- Child processes inherit parent's CSID
- `kill -9 %3` kills entire CSID group
- Session leaders create new CSID with `setsid()`

```c
/* Kill all processes in job #3 */
job_kill_num(3, SIGKILL);

/* Create new session */
csid_t new_session = proc_setsid();
```

## Security Model

### Capabilities
```c
#define CAP_KILL    0x01    /* Can signal any process */
#define CAP_SETUID  0x02    /* Can change UID/GID */
#define CAP_CHROOT  0x04    /* Can change root */
#define CAP_MOUNT   0x08    /* Can mount filesystems */
#define CAP_RAWIO   0x10    /* Raw I/O access */
#define CAP_ADMIN   0x20    /* Full admin */
```

### doas Integration
```c
/* Check if doas is allowed */
auth_result_t result = doas_check_rules(uid, target, cmd);

if (result == AUTH_OK) {
    /* Elevate privileges */
    p->euid = target_uid;
    p->caps = CAP_ALL;
    p->flags |= PROC_FLAG_ELEVATED;
}
```

### Process Validation
```c
void proc_invalidate(process_t* p)
{
    p->valid = 0;
    p->state = PROC_BLOCKED;
    sched_remove(p);
}
```

## SASY Integration

### On Process Creation
```c
/* Create heap segment */
seg_handle_t heap = sasy_create_heap(p->pid, initial_size);

/* Create stack segment */
seg_handle_t stack = sasy_create_stack(p->pid, STACK_SIZE);
```

### On Process Exit
```c
/* Release all segments */
sasy_release_process(p->pid);
```

### On Fork (Copy-on-Write)
```c
/* Clone segment with CoW */
child->segments[i] = sasy_clone(parent->segments[i], child->pid);
```

## Boot Sequence

The correct initialisation order in `kmain()`:

```
kmain
  → multiboot_init, vmm_init, pmm_init, kheap_init
  → tty_init, klog_init, vmm_init (full)
  → keyboard_init, rtc_init, idt_init
  → pmem_init, bfs_init / bfs_format
  → sasy_init
  → spm_init          ← Security Policy Manager (before proc)
  → proc_init         ← Process table
  → exec_init         ← ELF loader
  → sched_init        ← Scheduler
  → pipe_init         ← IPC pipes
  → minit_init        ← Orchestration layer
  → syscall_init
  → net_init
  → minit_register("keyboard") → minit_spawn
  → minit_register("shell")    → minit_spawn
  → ctx_switch to idle process (enters scheduler)
```

The kernel does **not** launch the terminal or shell directly.
All user-facing services are registered with minit and run as scheduled processes.

## minit Orchestration Layer

minit is the service orchestration layer that sits between the Process Manager and user-facing programs. It manages a tree of named kernel services.

### Purpose

- Provides a named registry of kernel services
- Spawns each service as a kernel-mode process via PMGR
- Tracks crash counts and disables exhausted nodes
- Uses a small fallback stack pool if kmalloc fails during spawn

### Node Structure

```c
typedef struct minit_node {
    char name[MINIT_NAME_MAX];
    process_t* proc;

    struct minit_node* parent;
    struct minit_node* children[MINIT_MAX_CHILDREN];
    int child_count;

    int base_priority;
    int effective_priority;

    int max_restarts;
    int crash_count;
    bool auto_restart;
    bool disabled;

    void (*entry_fn)(void);
} minit_node_t;
```

### API

```c
/* Initialise the orchestration layer */
void minit_init(void);

/* Register a named service, optionally under a parent node */
minit_node_t* minit_register(const char* name, minit_node_t* parent);

/* Spawn a kernel process for a registered node */
int minit_spawn(minit_node_t* node);

/* Notify minit of a crashed process */
void minit_notify_crash(process_t* proc);

/* Look up a node by its associated process pointer */
minit_node_t* minit_find_by_proc(process_t* proc);

/* Look up a node by name */
minit_node_t* minit_find_by_name(const char* name);

/* Dump the service tree to klog */
void minit_dump(void);
```

### Spawn Flow

`minit_spawn` creates a kernel-mode process (`PROC_FLAG_KERNEL`) via PMGR and wires it to `minit_kernel_trampoline`:

1. Disables preemption and masks interrupts around `proc_create` to avoid lock inversion with the timer ISR
2. Calls `proc_create(NULL)` and applies the service name + base priority
3. Attaches the PCB to the node and allocates a kernel stack (kmalloc, then fallback pool)
4. Builds a kernel context via `ctx_init_kernel` and marks the process ready

`minit_kernel_trampoline` re-enables preemption, calls `node->entry_fn()`, then marks the process as a zombie and yields back to the scheduler.

The trampoline step is critical. Without `sched_enable_preempt()`, `preempt_count` stays at 1 and the scheduler cannot switch away from the first spawned process.

### Stack Pool

minit carries a small static pool of 8 kernel stacks. The pool is only used if the primary `kmalloc` allocation fails during spawn.

### Boot Registration

```c
/* In kmain, after minit_init() */
minit_node_t* kb = minit_register("keyboard", minit_root);
kb->entry_fn = keyboard_service_run;
kb->auto_restart = true;
kb->base_priority = SCHED_DEFAULT_PRIORITY;
minit_spawn(kb);

minit_node_t* sh = minit_register("shell", minit_root);
sh->entry_fn = shell_entry;
sh->auto_restart = false;
sh->base_priority = SCHED_DEFAULT_PRIORITY;
minit_spawn(sh);
```

After registering and spawning services the kernel performs a context switch into the idle process, handing control to the scheduler.

## Keyboard Service

The keyboard service (`keyboard_service_run`) runs as a kernel process managed by minit under the name `"keyboard"`.

- Registered by `kmain` and spawned via `minit_spawn`
- Polls for PS/2 keyboard input when no IRQ has been seen
- Executes `sti; hlt` between polls to idle the CPU until an interrupt wakes it
- Distinct from `keyboard_handler()` (the IRQ1 ISR), which fills the scancode buffer

Source: `drivers/input/keyboard.c`

## Syscall Interface

| Syscall | Number | Description |
|---------|--------|-------------|
| `SYS_FORK` | 40 | Fork process |
| `SYS_WAIT` | 41 | Wait for child |
| `SYS_KILL` | 42 | Send signal |
| `SYS_GETPID` | 43 | Get process ID |
| `SYS_GETPPID` | 44 | Get parent PID |
| `SYS_SETSID` | 45 | Create session |
| `SYS_YIELD` | 46 | Yield CPU |
| `SYS_SLEEP` | 47 | Sleep (ms) |
| `SYS_GETUID` | 48 | Get user ID |
| `SYS_SETUID` | 49 | Set user ID |

## Userland API (libgrace)

```c
/* Process creation */
pid_t fork(void);
int exec(const char* path);
void exit(int code);

/* Process synchronization */
pid_t waitpid(pid_t pid, int* status, int options);
pid_t wait(int* status);

/* Process control */
int kill(pid_t pid, int signal);
void yield(void);
int sleep_ms(int ms);

/* Process info */
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
int setuid(uid_t uid);
int setsid(void);
```

## Init Process

PID 1 is the init process:
- Started by kernel after boot
- Owns orphaned children (reparenting)
- Reaps zombie orphans
- Path: `/sbin/init`

In the current implementation minit fulfils the role of init for kernel services. A full userland init process is a future milestone.

## Debug Tools

Shell commands:
```
ps      # List processes
jobs    # List background jobs
top     # Show CPU usage
tree    # Show process tree
kill    # Send signal to process
```

## Future Enhancements

- Per-CPU run queues for SMP
- NUMA awareness
- Load balancing
- Priority inheritance for mutexes
- Real-time scheduling class
- cgroups-style resource limits
- Full userland init process (PID 1)
- minit dependency ordering and parallel spawn
