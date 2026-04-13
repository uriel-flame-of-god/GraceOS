// ============================
// GraceOS Process Syscalls
// Kernel-side process management
// ============================

#include "../include/syscall.h"
#include "../proc/proc.h"
#include "../proc/sched.h"
#include "../spm/spm.h"
#include "../../drivers/video/tty.h"
#include "../log/klog.h"

/* ============================
   External Functions
   ============================ */

extern pid_t proc_fork(void);
extern void proc_exit(int code);
extern pid_t proc_wait(pid_t pid, int* status, int options);
extern int proc_kill(pid_t pid, int signal);
extern csid_t proc_setsid(void);
extern int proc_setuid(process_t* p, uid_t uid);
extern uint64_t timer_get_ms(void);

/* ============================
   SYS_FORK - Fork current process
   ============================ */

long sys_fork(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    return (long)proc_fork();
}

/* ============================
   SYS_WAIT - Wait for child process
   ============================ */

long sys_wait(long pid, long status_ptr, long options, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    
    int* status = (int*)status_ptr;
    return (long)proc_wait((pid_t)pid, status, (int)options);
}

/* ============================
   SYS_KILL - Send signal to process
   ============================ */

long sys_kill(long pid, long signal, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    uint32_t uid = current ? (uint32_t)current->uid : 0;
    if (spm_check(uid, PERM_KILL, "*") < 0)
        return -1;

    return (long)proc_kill((pid_t)pid, (int)signal);
}

/* ============================
   SYS_GETPID - Get current process ID
   ============================ */

long sys_getpid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    if (current)
        return (long)current->pid;
    
    return 0;
}

/* ============================
   SYS_GETPPID - Get parent process ID
   ============================ */

long sys_getppid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    if (current)
        return (long)current->parent;
    
    return 0;
}

/* ============================
   SYS_SETSID - Create new session
   ============================ */

long sys_setsid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    return (long)proc_setsid();
}

/* ============================
   SYS_YIELD - Yield CPU to scheduler
   ============================ */

long sys_yield(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    sched_yield();
    return 0;
}

/* ============================
   SYS_SLEEP - Sleep for milliseconds
   ============================ */

long sys_sleep(long ms, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    if (ms <= 0)
        return 0;
    
    if (!current)
        return 0;

    /* Record wakeup time, block this process, and yield the CPU.
       proc_check_sleepers() (called from sched_tick on every timer IRQ)
       will call sched_unblock() when the deadline passes. */
    current->sleep_until = timer_get_ms() + (uint64_t)ms;
    current->state       = PROC_BLOCKED;
    sched_remove(current);
    schedule();

    return 0;
}

/* ============================
   SYS_GETUID - Get user ID
   ============================ */

long sys_getuid(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    if (current)
        return (long)current->uid;
    
    return 0;
}

/* ============================
   SYS_SETUID - Set user ID
   ============================ */

long sys_setuid(long uid, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;

    if (!current)
        return -1;

    /* Build target string "uid:<id>" */
    char target[32];
    target[0] = 'u'; target[1] = 'i'; target[2] = 'd'; target[3] = ':';
    /* Simple uint-to-string into target+4 */
    uint32_t v = (uint32_t)uid;
    char tmp[16];
    int ti = 0;
    if (v == 0) { tmp[ti++] = '0'; }
    else { while (v > 0) { tmp[ti++] = '0' + (int)(v % 10); v /= 10; } }
    int oi = 4;
    for (int k = ti - 1; k >= 0 && oi < 30; k--)
        target[oi++] = tmp[k];
    target[oi] = '\0';

    uint32_t caller_uid = (uint32_t)current->uid;
    if (spm_check(caller_uid, PERM_GRANT, target) < 0)
        return -1;

    return (long)proc_setuid(current, (uid_t)uid);
}
