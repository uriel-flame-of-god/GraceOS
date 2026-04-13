// ============================
// GraceOS Wait Implementation
// Process synchronization
// ============================

#include "proc.h"
#include "sched.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   Wait Flags
   ============================ */

#define WNOHANG     0x01    /* Don't block if no child exited */
#define WUNTRACED   0x02    /* Report stopped children too */

/* ============================
   Wait Status Macros
   ============================ */

#define WEXITSTATUS(s)  (((s) >> 8) & 0xFF)
#define WTERMSIG(s)     ((s) & 0x7F)
#define WIFEXITED(s)    (WTERMSIG(s) == 0)
#define WIFSIGNALED(s)  (WTERMSIG(s) != 0)
#define WIFSTOPPED(s)   (((s) & 0xFF) == 0x7F)

/* ============================
   Find Zombie Child
   ============================ */

static process_t* find_zombie_child(pid_t parent_pid, pid_t target_pid)
{
    process_t* children[64];
    uint32_t count = proc_get_children(parent_pid, children, 64);
    
    for (uint32_t i = 0; i < count; i++)
    {
        process_t* child = children[i];
        
        /* Check if this is the target (or any child if target == -1) */
        if (target_pid != (pid_t)-1 && child->pid != target_pid)
            continue;
        
        /* Check if zombie */
        if (child->state == PROC_ZOMBIE)
            return child;
    }
    
    return NULL;
}

/* ============================
   Check if Process Has Children
   ============================ */

static int has_children(pid_t parent_pid, pid_t target_pid)
{
    process_t* children[64];
    uint32_t count = proc_get_children(parent_pid, children, 64);
    
    if (target_pid == (pid_t)-1)
        return count > 0;
    
    for (uint32_t i = 0; i < count; i++)
    {
        if (children[i]->pid == target_pid)
            return 1;
    }
    
    return 0;
}

/* ============================
   proc_wait - Wait for child
   
   pid:
     -1: Any child
     >0: Specific child
   
   Returns:
     PID of terminated child
     -1 on error
   ============================ */

pid_t proc_wait(pid_t pid, int* status, int options)
{
    process_t* parent = current;
    
    if (!parent)
        return -1;
    
    /* Check if we have any children to wait for */
    if (!has_children(parent->pid, pid))
    {
        /* No children: ECHILD */
        return -1;
    }
    
    /* Set up wait info */
    parent->wait.waiting_for = pid;
    parent->wait.status_ptr = status;
    
    while (1)
    {
        /* Look for zombie child */
        process_t* zombie = find_zombie_child(parent->pid, pid);
        
        if (zombie)
        {
            /* Found a zombie - collect it */
            pid_t child_pid = zombie->pid;
            
            /* Set status if requested */
            if (status)
            {
                /* Build status word */
                if (zombie->exit_signal)
                {
                    /* Killed by signal */
                    *status = zombie->exit_signal & 0x7F;
                }
                else
                {
                    /* Normal exit */
                    *status = (zombie->exit_code & 0xFF) << 8;
                }
            }
            
            /* Destroy the zombie */
            proc_destroy(zombie);
            
            return child_pid;
        }
        
        /* No zombie found */
        
        if (options & WNOHANG)
        {
            /* Non-blocking: return 0 */
            return 0;
        }
        
        /* Block until a child changes state */
        parent->state = PROC_WAIT;
        sched_sleep();
        
        /* Check if we still have children */
        if (!has_children(parent->pid, pid))
        {
            return -1;
        }
    }
}

/* ============================
   waitpid - Wait for specific PID
   ============================ */

pid_t proc_waitpid(pid_t pid, int* status, int options)
{
    return proc_wait(pid, status, options);
}

/* ============================
   wait - Wait for any child
   ============================ */

pid_t proc_wait_any(int* status)
{
    return proc_wait(-1, status, 0);
}
