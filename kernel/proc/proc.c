// ============================
// GraceOS Process Manager
// Core process operations
// ============================

#include "proc.h"
#include "sched.h"
#include "../mm/kheap.h"
#include "../mm/sasy/sasy.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   External Timer
   ============================ */

extern uint64_t timer_get_ms(void);

/* ============================
   Table Operations (from table.c)
   ============================ */

extern void proc_table_init(void);
extern void proc_table_add(process_t* p);
extern void proc_table_remove(pid_t pid);

/* ============================
   Process Slab Allocator
   
   For now, use kheap. Later optimize
   with dedicated slab cache.
   ============================ */

static process_t* proc_alloc(void)
{
    return (process_t*)kmalloc(sizeof(process_t));
}

static void proc_free_mem(process_t* p)
{
    kfree(p);
}

/* ============================
   Process Initialization
   ============================ */

void proc_init(void)
{
    /* Initialize process table */
    proc_table_init();
    
    klog_init_msg("Process manager initialized");
}

/* ============================
   Process Creation
   ============================ */

process_t* proc_create(process_t* parent)
{
    /* Allocate PCB */
    process_t* p = proc_alloc();
    if (!p)
    {
        klog_error("Failed to allocate process");
        return NULL;
    }
    
    /* Zero initialize */
    memset(p, 0, sizeof(process_t));
    
    /* Assign PID */
    p->pid = proc_alloc_pid();
    if (p->pid == PID_INVALID)
    {
        klog_error("No PIDs available");
        proc_free_mem(p);
        return NULL;
    }
    
    /* Set parent and CSID */
    if (parent)
    {
        p->parent = parent->pid;
        p->csid = parent->csid;  /* Inherit CSID from parent */
        
        /* Inherit credentials */
        p->uid = parent->uid;
        p->euid = parent->euid;
        p->gid = parent->gid;
        p->egid = parent->egid;
        p->caps = parent->caps;
        
        /* Inherit priority */
        p->base_priority = parent->base_priority;
        p->nice = parent->nice;
    }
    else
    {
        /* New session leader */
        p->csid = proc_alloc_csid();
        p->parent = PID_KERNEL;
        
        /* Default credentials (root for now) */
        p->uid = 0;
        p->euid = 0;
        p->gid = 0;
        p->egid = 0;
        p->caps = CAP_ALL;
        
        /* Default priority */
        p->base_priority = SCHED_DEFAULT_PRIORITY;
        p->nice = 0;
    }
    
    /* Initialize state */
    p->state = PROC_NEW;
    p->valid = 1;  /* Initially valid */
    p->flags = PROC_FLAG_NONE;
    
    /* Initialize timing */
    p->start_time = timer_get_ms();
    p->cpu_time = 0;
    p->est_runtime = 100;  /* Default estimate: 100ms */
    p->wait_time = 0;
    p->last_run = 0;
    
    /* Initialize scheduling */
    p->current_priority = p->base_priority;
    
    /* Initialize segment handles */
    for (int i = 0; i < PROC_MAX_SEGMENTS; i++)
    {
        p->segments[i] = INVALID_HANDLE;
    }
    
    /* Initialize file descriptors */
    for (int i = 0; i < PROC_MAX_FDS; i++)
    {
        p->fds[i] = NULL;
    }
    
    /* Initialize wait info */
    p->wait.waiting_for = PID_INVALID;
    p->wait.status_ptr = NULL;
    
    /* Initialize linked list pointers */
    p->next = NULL;
    p->prev = NULL;
    
    /* Add to process table */
    proc_table_add(p);
    
    return p;
}

/* ============================
   Process Destruction
   ============================ */

void proc_destroy(process_t* p)
{
    if (!p)
        return;
    
    /* Reparent children to init */
    proc_reparent_children(p->pid);
    
    /* Release SASY segments */
    sasy_release_process(p->pid);
    
    /* Close file descriptors */
    for (int i = 0; i < PROC_MAX_FDS; i++)
    {
        if (p->fds[i] != NULL)
        {
            /* TODO: Close file */
            p->fds[i] = NULL;
        }
    }
    
    /* Free page tables */
    if (p->pml4 != 0)
    {
        /* TODO: vmm_destroy_space(p->pml4); */
    }
    
    /* Destroy IPC namespace */
    if (p->ipc != NULL)
    {
        /* TODO: ipc_destroy_ns(p->ipc); */
        p->ipc = NULL;
    }
    
    /* Remove from process table */
    proc_table_remove(p->pid);
    
    /* Mark as dead */
    p->state = PROC_DEAD;
    
    /* Free PCB memory */
    proc_free_mem(p);
}

/* ============================
   State Transitions
   ============================ */

void proc_set_ready(process_t* p)
{
    if (!p)
        return;
    
    if (p->state == PROC_ZOMBIE || p->state == PROC_DEAD)
        return;  /* Can't resurrect */
    
    p->state = PROC_READY;
    sched_add(p);
}

void proc_set_blocked(process_t* p)
{
    if (!p)
        return;
    
    p->state = PROC_BLOCKED;
    sched_remove(p);
}

void proc_set_zombie(process_t* p, int exit_code)
{
    if (!p)
        return;
    
    p->state = PROC_ZOMBIE;
    p->exit_code = exit_code;
    sched_remove(p);
    
    /* Wake parent if waiting */
    sched_wake_parent(p->pid);
}

/* ============================
   Process Name
   ============================ */

void proc_set_name(process_t* p, const char* name)
{
    if (!p || !name)
        return;
    
    strncpy(p->name, name, PROC_NAME_MAX - 1);
    p->name[PROC_NAME_MAX - 1] = '\0';
}

/* ============================
   Debug Functions
   ============================ */

static const char* state_names[] = {
    "NEW",
    "READY",
    "RUNNING",
    "WAIT",
    "BLOCKED",
    "ZOMBIE",
    "DEAD"
};

void proc_dump(process_t* p)
{
    if (!p)
        return;
    
    /* Would need tty_print for actual output */
    /* Placeholder for now */
    (void)state_names;
}

/* Callback for proc_dump_all */
static void dump_proc_cb(process_t* p, void* arg)
{
    (void)arg;
    proc_dump(p);
}

void proc_dump_all(void)
{
    extern void proc_foreach(void (*callback)(process_t*, void*), void*);
    proc_foreach(dump_proc_cb, NULL);
}

void proc_dump_tree(void)
{
    /* TODO: Implement tree view */
}
