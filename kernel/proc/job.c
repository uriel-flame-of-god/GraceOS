// ============================
// GraceOS Job Control
// CSID-based job trees
// ============================

#include "proc.h"
#include "sched.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   Signal Definitions
   ============================ */

#define SIGKILL     9       /* Kill (cannot be caught) */
#define SIGTERM     15      /* Terminate */
#define SIGSTOP     19      /* Stop (cannot be caught) */
#define SIGCONT     18      /* Continue */
#define SIGHUP      1       /* Hangup */
#define SIGINT      2       /* Interrupt (Ctrl+C) */
#define SIGQUIT     3       /* Quit */

/* ============================
   Job States
   ============================ */

typedef enum job_state {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

/* ============================
   Job Structure
   ============================ */

typedef struct job {
    csid_t csid;                /* Command Session ID */
    pid_t leader;               /* Session leader PID */
    job_state_t state;          /* Job state */
    char command[64];           /* Command line (for display) */
    int job_num;                /* Job number (for %N) */
} job_t;

/* ============================
   Job Table
   ============================ */

#define MAX_JOBS 64

static job_t jobs[MAX_JOBS];
static int next_job_num = 1;
static volatile int job_lock = 0;

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_jobs(void)
{
    while (__sync_lock_test_and_set(&job_lock, 1))
    {
        #ifdef ARCH_ARM64
        __asm__ volatile ("yield");
        #else
        __asm__ volatile ("pause");
        #endif
    }
}

static inline void unlock_jobs(void)
{
    __sync_lock_release(&job_lock);
}

/* ============================
   Job Management
   ============================ */

/* Create a new job for a CSID */
int job_create(csid_t csid, pid_t leader, const char* cmd)
{
    lock_jobs();
    
    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs[i].csid == 0)
        {
            slot = i;
            break;
        }
    }
    
    if (slot < 0)
    {
        unlock_jobs();
        return -1;  /* No slots available */
    }
    
    jobs[slot].csid = csid;
    jobs[slot].leader = leader;
    jobs[slot].state = JOB_RUNNING;
    jobs[slot].job_num = next_job_num++;
    
    if (cmd)
    {
        strncpy(jobs[slot].command, cmd, 63);
        jobs[slot].command[63] = '\0';
    }
    else
    {
        jobs[slot].command[0] = '\0';
    }
    
    unlock_jobs();
    return jobs[slot].job_num;
}

/* Find job by CSID */
job_t* job_find_by_csid(csid_t csid)
{
    lock_jobs();
    
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs[i].csid == csid)
        {
            unlock_jobs();
            return &jobs[i];
        }
    }
    
    unlock_jobs();
    return NULL;
}

/* Find job by job number (%N) */
job_t* job_find_by_num(int job_num)
{
    lock_jobs();
    
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs[i].job_num == job_num && jobs[i].csid != 0)
        {
            unlock_jobs();
            return &jobs[i];
        }
    }
    
    unlock_jobs();
    return NULL;
}

/* Remove job */
void job_remove(csid_t csid)
{
    lock_jobs();
    
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs[i].csid == csid)
        {
            memset(&jobs[i], 0, sizeof(job_t));
            break;
        }
    }
    
    unlock_jobs();
}

/* ============================
   Signal Delivery
   ============================ */

/* Send signal to a process */
int proc_kill(pid_t pid, int signal)
{
    process_t* p = proc_find(pid);
    if (!p)
        return -1;  /* No such process */
    
    /* Check permissions */
    process_t* sender = current;
    if (sender)
    {
        /* Root can signal anyone */
        if (sender->uid != 0)
        {
            /* Non-root can only signal own processes */
            if (p->uid != sender->uid && p->uid != sender->euid)
            {
                return -1;  /* Permission denied */
            }
        }
    }
    
    /* Handle signal */
    switch (signal)
    {
        case SIGKILL:
            /* Immediate termination */
            proc_terminate(p, SIGKILL);
            break;
        
        case SIGTERM:
            /* Request termination */
            proc_terminate(p, SIGTERM);
            break;
        
        case SIGSTOP:
            /* Stop process */
            p->state = PROC_BLOCKED;
            sched_remove(p);
            break;
        
        case SIGCONT:
            /* Continue stopped process */
            if (p->state == PROC_BLOCKED)
            {
                p->state = PROC_READY;
                sched_add(p);
            }
            break;
        
        default:
            /* TODO: Signal handler support */
            proc_terminate(p, signal);
            break;
    }
    
    return 0;
}

/* ============================
   CSID Group Operations
   ============================ */

/* Kill all processes in a CSID group */
int job_kill_csid(csid_t csid, int signal)
{
    process_t* procs[64];
    uint32_t count = proc_get_csid_group(csid, procs, 64);
    
    if (count == 0)
        return -1;  /* No such job */
    
    int result = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (proc_kill(procs[i]->pid, signal) < 0)
            result = -1;
    }
    
    /* Update job state */
    job_t* job = job_find_by_csid(csid);
    if (job)
    {
        if (signal == SIGKILL || signal == SIGTERM)
            job->state = JOB_DONE;
        else if (signal == SIGSTOP)
            job->state = JOB_STOPPED;
        else if (signal == SIGCONT)
            job->state = JOB_RUNNING;
    }
    
    return result;
}

/* Kill by job number (shell command: kill %N) */
int job_kill_num(int job_num, int signal)
{
    job_t* job = job_find_by_num(job_num);
    if (!job)
        return -1;
    
    return job_kill_csid(job->csid, signal);
}

/* ============================
   New Session
   ============================ */

/* Create new session (setsid equivalent) */
csid_t proc_setsid(void)
{
    process_t* p = current;
    if (!p)
        return 0;
    
    /* Already a session leader? */
    if (p->pid == p->csid)
        return 0;  /* Error: already leader */
    
    /* Create new session */
    csid_t new_csid = proc_alloc_csid();
    p->csid = new_csid;
    
    return new_csid;
}

/* ============================
   Job Listing (for shell)
   ============================ */

/* Get list of all jobs */
int job_list(job_t** out, int max)
{
    int count = 0;
    
    lock_jobs();
    
    for (int i = 0; i < MAX_JOBS && count < max; i++)
    {
        if (jobs[i].csid != 0)
        {
            out[count++] = &jobs[i];
        }
    }
    
    unlock_jobs();
    return count;
}

/* ============================
   Foreground / Background
   ============================ */

/* Bring job to foreground */
int job_fg(int job_num)
{
    job_t* job = job_find_by_num(job_num);
    if (!job)
        return -1;
    
    /* Send SIGCONT if stopped */
    if (job->state == JOB_STOPPED)
    {
        job_kill_csid(job->csid, SIGCONT);
    }
    
    job->state = JOB_RUNNING;
    
    /* TODO: Set as foreground process group */
    
    return 0;
}

/* Send job to background */
int job_bg(int job_num)
{
    job_t* job = job_find_by_num(job_num);
    if (!job)
        return -1;
    
    /* Send SIGCONT if stopped */
    if (job->state == JOB_STOPPED)
    {
        job_kill_csid(job->csid, SIGCONT);
    }
    
    job->state = JOB_RUNNING;
    
    return 0;
}

/* ============================
   Init Function
   ============================ */

void job_init(void)
{
    memset(jobs, 0, sizeof(jobs));
    next_job_num = 1;
    
    klog_init_msg("Job control initialized");
}
