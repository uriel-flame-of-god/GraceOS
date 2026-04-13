// ============================
// GraceOS Fork Implementation
// Process cloning
// ============================

#include "proc.h"
#include "sched.h"
#include "context.h"
#include "../mm/kheap.h"
#include "../mm/vmm/vmm.h"
#include "../mm/sasy/sasy.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   Clone Address Space
   
   Creates a copy of the parent's page tables
   with copy-on-write semantics.
   ============================ */

static int clone_address_space(process_t* parent, process_t* child)
{
    if (!parent || !child)
        return -1;
    
    /* Create new page table root */
    /* TODO: child->pml4 = vmm_clone_space(parent->pml4); */
    
    /* For now, just copy the PML4 reference */
    /* Real implementation would set up CoW */
    child->pml4 = parent->pml4;
    
    return 0;
}

/* ============================
   Copy SASY Segments
   
   Clones all segments using SASY's CoW support.
   ============================ */

static int copy_segments(process_t* parent, process_t* child)
{
    if (!parent || !child)
        return -1;
    
    for (int i = 0; i < PROC_MAX_SEGMENTS; i++)
    {
        if (parent->segments[i] != INVALID_HANDLE)
        {
            /* Clone segment with copy-on-write */
            child->segments[i] = sasy_clone(parent->segments[i], child->pid);
        }
        else
        {
            child->segments[i] = INVALID_HANDLE;
        }
    }
    
    return 0;
}

/* ============================
   Copy File Descriptors
   
   Duplicates the file descriptor table.
   ============================ */

static int copy_fds(process_t* parent, process_t* child)
{
    if (!parent || !child)
        return -1;
    
    for (int i = 0; i < PROC_MAX_FDS; i++)
    {
        if (parent->fds[i] != NULL)
        {
            /* TODO: child->fds[i] = fd_dup(parent->fds[i]); */
            /* For now, just copy the pointer (shared) */
            child->fds[i] = parent->fds[i];
        }
        else
        {
            child->fds[i] = NULL;
        }
    }
    
    return 0;
}

/* ============================
   Copy CPU Context
   
   The child gets a copy of parent's context,
   but will return 0 from fork().
   ============================ */

static void copy_context(process_t* parent, process_t* child)
{
    if (!parent || !child)
        return;
    
    /* Copy CPU context */
    memcpy(&child->ctx, &parent->ctx, sizeof(struct cpu_context));
    
    /* Copy FPU context */
    memcpy(&child->fpu, &parent->fpu, sizeof(struct fpu_context));
    
    /* Child's fork() returns 0 (set RAX in context) */
    /* Since we use callee-saved regs, we'll handle this 
       when actually returning to userspace */
}

/* ============================
   proc_fork - Fork current process
   
   Returns:
     > 0: Child PID (to parent)
       0: (to child) - handled by context setup
      -1: Error
   ============================ */

pid_t proc_fork(void)
{
    process_t* parent = current;
    
    if (!parent)
    {
        klog_error("fork: No current process");
        return -1;
    }
    
    /* Check if forking allowed */
    if (parent->flags & PROC_FLAG_NOFORK)
    {
        klog_error("fork: Process cannot fork");
        return -1;
    }
    
    /* Create child process */
    process_t* child = proc_create(parent);
    if (!child)
    {
        klog_error("fork: Failed to create child process");
        return -1;
    }
    
    /* Copy process name */
    memcpy(child->name, parent->name, PROC_NAME_MAX);
    
    /* Clone address space */
    if (clone_address_space(parent, child) < 0)
    {
        proc_destroy(child);
        return -1;
    }
    
    /* Copy segments */
    if (copy_segments(parent, child) < 0)
    {
        proc_destroy(child);
        return -1;
    }
    
    /* Copy file descriptors */
    if (copy_fds(parent, child) < 0)
    {
        proc_destroy(child);
        return -1;
    }
    
    /* Copy context */
    copy_context(parent, child);
    
    /* Copy memory layout info */
    child->brk = parent->brk;
    child->stack_base = parent->stack_base;
    child->stack_size = parent->stack_size;
    
    /* Copy timing estimates */
    child->est_runtime = parent->est_runtime;
    
    /* Add child to scheduler */
    child->state = PROC_READY;
    sched_add(child);
    
    /* Return child PID to parent */
    return child->pid;
}

/* ============================
   vfork - Lightweight fork
   
   Child shares address space with parent.
   Parent is blocked until child calls exec() or exit().
   
   Used for fork+exec pattern optimization.
   ============================ */

pid_t proc_vfork(void)
{
    process_t* parent = current;
    
    if (!parent)
        return -1;
    
    /* Create child */
    process_t* child = proc_create(parent);
    if (!child)
        return -1;
    
    /* Share address space (no copy) */
    child->pml4 = parent->pml4;
    
    /* Share segments (reference, no clone) */
    for (int i = 0; i < PROC_MAX_SEGMENTS; i++)
    {
        if (parent->segments[i] != INVALID_HANDLE)
        {
            handle_addref(parent->segments[i]);
            child->segments[i] = parent->segments[i];
        }
    }
    
    /* Copy file descriptors */
    copy_fds(parent, child);
    
    /* Copy context */
    copy_context(parent, child);
    
    /* Block parent until child exits or execs */
    parent->state = PROC_BLOCKED;
    sched_remove(parent);
    
    /* Schedule child */
    child->state = PROC_READY;
    sched_add(child);
    
    return child->pid;
}
