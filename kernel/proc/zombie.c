// ============================
// GraceOS Zombie Handling
// Process exit and cleanup
// ============================

#include "proc.h"
#include "sched.h"
#include "../mm/sasy/sasy.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   proc_exit - Exit current process
   
   Cleans up resources and becomes zombie.
   Parent must call wait() to fully reap.
   ============================ */

void proc_exit(int code)
{
    process_t* p = current;
    
    if (!p)
    {
        klog_error("exit: No current process");
        return;
    }
    
    /* Prevent double-exit */
    if (p->state == PROC_ZOMBIE || p->state == PROC_DEAD)
    {
        klog_warn("exit: Process already exited");
        return;
    }
    
    /* Store exit code */
    p->exit_code = code;
    p->exit_signal = 0;
    
    /* Release SASY segments */
    sasy_release_process(p->pid);
    
    /* Clear segment handles */
    for (int i = 0; i < PROC_MAX_SEGMENTS; i++)
    {
        p->segments[i] = INVALID_HANDLE;
    }
    
    /* Close file descriptors */
    for (int i = 0; i < PROC_MAX_FDS; i++)
    {
        if (p->fds[i] != NULL)
        {
            /* TODO: Close file properly */
            p->fds[i] = NULL;
        }
    }
    
    /* Destroy IPC namespace */
    if (p->ipc != NULL)
    {
        /* TODO: ipc_destroy_ns(p->ipc); */
        p->ipc = NULL;
    }
    
    /* Reparent children to init */
    proc_reparent_children(p->pid);
    
    /* Become zombie */
    p->state = PROC_ZOMBIE;
    
    /* Remove from scheduler */
    sched_remove(p);
    
    /* Wake parent if waiting */
    sched_wake_parent(p->pid);
    
    /* Never return - schedule another process */
    schedule();
    
    /* Should never reach here */
    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}

/* ============================
   proc_terminate - Kill by signal
   
   Similar to exit but sets exit_signal.
   ============================ */

void proc_terminate(process_t* p, int signal)
{
    if (!p)
        return;
    
    /* Store signal */
    p->exit_signal = signal;
    p->exit_code = 0;
    
    /* Release resources */
    sasy_release_process(p->pid);
    
    /* Clear segments */
    for (int i = 0; i < PROC_MAX_SEGMENTS; i++)
    {
        p->segments[i] = INVALID_HANDLE;
    }
    
    /* Close fds */
    for (int i = 0; i < PROC_MAX_FDS; i++)
    {
        p->fds[i] = NULL;
    }
    
    /* Reparent children */
    proc_reparent_children(p->pid);
    
    /* Become zombie */
    p->state = PROC_ZOMBIE;
    sched_remove(p);
    sched_wake_parent(p->pid);
    
    /* If this was current, reschedule */
    if (p == current)
    {
        schedule();
    }
}

/* ============================
   proc_reap_zombies
   
   Cleanup orphaned zombies whose parent
   is init (PID 1). Init should call this
   periodically.
   ============================ */

void proc_reap_zombies(void)
{
    process_t* children[64];
    uint32_t count = proc_get_children(PID_INIT, children, 64);
    
    for (uint32_t i = 0; i < count; i++)
    {
        if (children[i]->state == PROC_ZOMBIE)
        {
            proc_destroy(children[i]);
        }
    }
}

/* ============================
   panic_exit - Emergency exit
   
   Called when process triggers kernel panic.
   Attempts to clean up before halting.
   ============================ */

void panic_exit(process_t* p)
{
    if (!p)
        return;
    
    klog_fail("Process panic - emergency cleanup");
    
    /* Force cleanup */
    p->state = PROC_DEAD;
    sched_remove(p);
    
    /* Release what we can */
    sasy_release_process(p->pid);
    
    /* Reparent children */
    proc_reparent_children(p->pid);
    
    /* If current, try to schedule next */
    if (p == current)
    {
        current = NULL;
        schedule();
    }
}
