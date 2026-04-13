// ============================
// GraceOS Process Table
// Global process management
// ============================

#include "proc.h"
#include "sched.h"
#include "../mm/kheap.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   Global Process Table
   ============================ */

static process_t* proc_table[MAX_PROC];
static volatile int proc_lock = 0;

/* ============================
   PID / CSID Allocation
   ============================ */

static pid_t next_pid = 1;
static csid_t next_csid = 1;

/* ============================
   Current Process Pointer
   ============================ */

process_t* current = NULL;

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_proctable(void)
{
    while (__sync_lock_test_and_set(&proc_lock, 1))
    {
        __asm__ volatile ("pause");
    }
}

static inline void unlock_proctable(void)
{
    __sync_lock_release(&proc_lock);
}

/* ============================
   PID Allocation
   ============================ */

pid_t proc_alloc_pid(void)
{
    lock_proctable();
    
    pid_t pid = next_pid;
    
    /* Find next available PID */
    while (pid < MAX_PROC && proc_table[pid] != NULL)
    {
        pid++;
    }
    
    if (pid >= MAX_PROC)
    {
        /* Wrap around and search from 1 */
        pid = 1;
        while (pid < next_pid && proc_table[pid] != NULL)
        {
            pid++;
        }
        
        if (pid >= next_pid)
        {
            unlock_proctable();
            return PID_INVALID;  /* No PIDs available */
        }
    }
    
    next_pid = pid + 1;
    if (next_pid >= MAX_PROC)
    {
        next_pid = 1;
    }
    
    unlock_proctable();
    return pid;
}

/* ============================
   CSID Allocation
   ============================ */

csid_t proc_alloc_csid(void)
{
    lock_proctable();
    csid_t csid = next_csid++;
    if (next_csid == 0) next_csid = 1;  /* Wrap around, skip 0 */
    unlock_proctable();
    return csid;
}

/* ============================
   Table Operations
   ============================ */

/* Add process to table */
void proc_table_add(process_t* p)
{
    if (!p || p->pid == PID_INVALID || p->pid >= MAX_PROC)
        return;
    
    lock_proctable();
    proc_table[p->pid] = p;
    unlock_proctable();
}

/* Remove process from table */
void proc_table_remove(pid_t pid)
{
    if (pid == PID_INVALID || pid >= MAX_PROC)
        return;
    
    lock_proctable();
    proc_table[pid] = NULL;
    unlock_proctable();
}

/* Find process by PID */
process_t* proc_find(pid_t pid)
{
    if (pid == PID_INVALID || pid >= MAX_PROC)
        return NULL;
    
    lock_proctable();
    process_t* p = proc_table[pid];
    unlock_proctable();
    
    return p;
}

/* ============================
   Process Counting
   ============================ */

uint32_t proc_count(void)
{
    uint32_t count = 0;
    
    lock_proctable();
    for (uint32_t i = 0; i < MAX_PROC; i++)
    {
        if (proc_table[i] != NULL && proc_table[i]->state != PROC_DEAD)
        {
            count++;
        }
    }
    unlock_proctable();
    
    return count;
}

/* ============================
   CSID Group Operations
   ============================ */

uint32_t proc_get_csid_group(csid_t csid, process_t** out, uint32_t max)
{
    uint32_t count = 0;
    
    lock_proctable();
    for (uint32_t i = 0; i < MAX_PROC && count < max; i++)
    {
        if (proc_table[i] != NULL && proc_table[i]->csid == csid)
        {
            out[count++] = proc_table[i];
        }
    }
    unlock_proctable();
    
    return count;
}

/* ============================
   Children Operations
   ============================ */

uint32_t proc_get_children(pid_t parent, process_t** out, uint32_t max)
{
    uint32_t count = 0;
    
    lock_proctable();
    for (uint32_t i = 0; i < MAX_PROC && count < max; i++)
    {
        if (proc_table[i] != NULL && proc_table[i]->parent == parent)
        {
            out[count++] = proc_table[i];
        }
    }
    unlock_proctable();
    
    return count;
}

/* Reparent orphaned children to init */
void proc_reparent_children(pid_t old_parent)
{
    lock_proctable();
    for (uint32_t i = 0; i < MAX_PROC; i++)
    {
        if (proc_table[i] != NULL && proc_table[i]->parent == old_parent)
        {
            proc_table[i]->parent = PID_INIT;
            proc_table[i]->flags |= PROC_FLAG_ORPHAN;
        }
    }
    unlock_proctable();
}

/* ============================
   Iteration Helpers
   ============================ */

/* Iterate all processes with callback */
void proc_foreach(void (*callback)(process_t* p, void* arg), void* arg)
{
    lock_proctable();
    for (uint32_t i = 0; i < MAX_PROC; i++)
    {
        if (proc_table[i] != NULL)
        {
            callback(proc_table[i], arg);
        }
    }
    unlock_proctable();
}

/* ============================
   Sleep Wakeup Check
   ============================ */

/* Walk the process table and unblock any sleeping process whose
   sleep_until timer has expired.  Called from sched_tick() on every
   timer interrupt (1000 Hz). */
void proc_check_sleepers(void)
{
    extern uint64_t timer_get_ms(void);
    uint64_t now = timer_get_ms();

    /* Avoid taking the lock while calling sched_unblock (which takes its own
       lock).  Snapshot the candidates first. */
    process_t* wake[64];
    int wake_count = 0;

    lock_proctable();
    for (uint32_t i = 0; i < MAX_PROC && wake_count < 64; i++)
    {
        process_t* p = proc_table[i];
        if (p && p->state == PROC_BLOCKED && p->sleep_until > 0 &&
            now >= p->sleep_until)
        {
            p->sleep_until = 0;
            wake[wake_count++] = p;
        }
    }
    unlock_proctable();

    for (int i = 0; i < wake_count; i++)
        sched_unblock(wake[i]);
}

/* ============================
   Table Initialization
   ============================ */

void proc_table_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    next_csid = 1;
    current = NULL;
    
    klog_log("Process table initialized");
}
