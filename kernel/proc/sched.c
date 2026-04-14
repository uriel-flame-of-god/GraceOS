// ============================
// GraceOS Hybrid Scheduler
// SJF + FCFS + Aging
// ============================

#include "sched.h"
#include "proc.h"
#include "context.h"
#include "../mm/kheap.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   External Timer
   ============================ */

extern uint64_t timer_get_ms(void);

/* Forward declaration for sleeper wakeup (implemented in table.c) */
extern void proc_check_sleepers(void);

/* ============================
   Run Queue
   Simple linked list for now.
   Later: per-priority queues.
   ============================ */

static process_t* run_queue_head = NULL;
static process_t* run_queue_tail = NULL;

/* ============================
   Idle Process
   ============================ */

static process_t idle_proc;
static uint8_t idle_stack[4096] __attribute__((aligned(16)));

/* ============================
   Scheduler State
   ============================ */

static volatile int sched_lock = 0;
static volatile int preempt_count = 0;
static sched_stats_t stats;

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_sched(void)
{
    while (__sync_lock_test_and_set(&sched_lock, 1))
    {
        __asm__ volatile ("pause");
    }
}

static inline void unlock_sched(void)
{
    __sync_lock_release(&sched_lock);
}

/* ============================
   Idle Process Entry
   ============================ */

static void idle_entry(void)
{
    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}

/* ============================
   Scheduler Initialization
   ============================ */

void sched_init(void)
{
    /* Clear statistics */
    memset(&stats, 0, sizeof(stats));
    
    /* Initialize idle process */
    memset(&idle_proc, 0, sizeof(idle_proc));
    idle_proc.pid = PID_KERNEL;
    idle_proc.csid = 0;
    idle_proc.state = PROC_READY;
    idle_proc.valid = 1;
    idle_proc.flags = PROC_FLAG_KERNEL;
    strcpy(idle_proc.name, "idle");
    idle_proc.base_priority = SCHED_MIN_PRIORITY;
    idle_proc.current_priority = SCHED_MIN_PRIORITY;
    
    /* Initialize idle context */
    ctx_init_kernel(&idle_proc.ctx, idle_entry, &idle_stack[4096]);
    
    /* Start with idle as current */
    current = &idle_proc;
    
    klog_init_msg("Scheduler initialized (Hybrid SJF+FCFS+Aging)");
}

/* ============================
   Run Queue Operations
   ============================ */

void sched_add(process_t* p)
{
    if (!p || p->state == PROC_DEAD || p->state == PROC_ZOMBIE)
        return;
    
    lock_sched();
    
    /* Already in queue? */
    if (p->next != NULL || p->prev != NULL || p == run_queue_head)
    {
        unlock_sched();
        return;
    }
    
    /* Add to tail */
    p->next = NULL;
    p->prev = run_queue_tail;
    
    if (run_queue_tail)
    {
        run_queue_tail->next = p;
    }
    else
    {
        run_queue_head = p;
    }
    
    run_queue_tail = p;
    
    stats.runnable_count++;
    
    unlock_sched();
}

void sched_remove(process_t* p)
{
    if (!p)
        return;
    
    lock_sched();
    
    /* Update links */
    if (p->prev)
        p->prev->next = p->next;
    else
        run_queue_head = p->next;
    
    if (p->next)
        p->next->prev = p->prev;
    else
        run_queue_tail = p->prev;
    
    p->next = NULL;
    p->prev = NULL;
    
    if (stats.runnable_count > 0)
        stats.runnable_count--;
    
    unlock_sched();
}

/* ============================
   Score Calculation
   
   Hybrid Algorithm:
   - SJF: Prefer shorter estimated runtime
   - FCFS: Aging decreases score over time
   - Security: Invalid processes penalized
   ============================ */

int64_t sched_calc_score(process_t* p)
{
    if (!p)
        return INT64_MAX;
    
    int64_t score = (int64_t)p->est_runtime;
    
    /* Aging: reduce score based on wait time */
    score -= (int64_t)(p->wait_time / SCHED_AGING_FACTOR);
    
    /* Priority adjustment */
    score -= (p->current_priority * 10);
    
    /* Invalid process penalty */
    if (!p->valid)
    {
        score += SCHED_INVALID_PENALTY;
    }
    
    /* Ensure score doesn't go too negative */
    if (score < INT64_MIN / 2)
        score = INT64_MIN / 2;
    
    return score;
}

/* ============================
   Pick Next Process
   
   Selects process with lowest score.
   ============================ */

process_t* sched_pick(void)
{
    lock_sched();
    
    process_t* best = NULL;
    int64_t best_score = INT64_MAX;
    
    for (process_t* p = run_queue_head; p != NULL; p = p->next)
    {
        /* Skip non-ready processes */
        if (p->state != PROC_READY)
            continue;
        
        /* Skip invalid processes unless no alternative */
        if (!p->valid && best != NULL)
            continue;
        
        int64_t score = sched_calc_score(p);
        
        if (score < best_score)
        {
            best = p;
            best_score = score;
        }
        else if (score == best_score && best != NULL)
        {
            /* Tie-breaker: prefer the process that waited longer. */
            if (p->wait_time > best->wait_time)
            {
                best = p;
            }
            /* If wait times are equal, avoid sticking to current forever. */
            else if (p->wait_time == best->wait_time && best == current && p != current)
            {
                best = p;
            }
        }
    }
    
    unlock_sched();
    
    /* Return idle if nothing else */
    return best ? best : &idle_proc;
}

/* ============================
   Main Scheduling Function
   ============================ */

void schedule(void)
{
    if (!sched_preempt_enabled())
        return;
    
    sched_disable_preempt();
    
    process_t* prev = current;
    process_t* next = sched_pick();
    
    /* Update state */
    if (prev && prev->state == PROC_RUNNING)
    {
        prev->state = PROC_READY;
    }
    
    if (next != prev)
    {
        /* Update timing */
        uint64_t now = timer_get_ms();
        if (prev)
        {
            prev->cpu_time += (now - prev->last_run);
        }
        
        /* Set next as running */
        next->state = PROC_RUNNING;
        next->last_run = now;
        next->wait_time = 0;  /* Reset wait time */
        
        /* Update current */
        current = next;
        
        /* Update statistics */
        stats.context_switches++;
        
        /* Context switch */
        if (prev)
        {
            ctx_switch(&prev->ctx, &next->ctx);
        }
    }
    else if (next)
    {
        /* Keep state coherent when the same process continues running. */
        next->state = PROC_RUNNING;
    }
    
    sched_enable_preempt();
}

/* ============================
   Yield CPU
   ============================ */

void sched_yield(void)
{
    if (current)
    {
        current->state = PROC_READY;
    }
    schedule();
}

/* ============================
   Timer Tick Handler
   
   Called from timer IRQ.
   Increments wait_time for aging.
   ============================ */

void sched_tick(void)
{
    stats.total_ticks++;
    
    int have_runnable = 0;

    lock_sched();
    
    /* Update wait times for aging */
    for (process_t* p = run_queue_head; p != NULL; p = p->next)
    {
        if (p->state == PROC_READY && p != current)
        {
            p->wait_time++;
        }
    }

    have_runnable = (run_queue_head != NULL);
    
    /* Track idle time */
    if (current == &idle_proc)
    {
        stats.idle_time++;
    }
    
    unlock_sched();

    /* Wake sleeping processes whose timer has expired */
    proc_check_sleepers();
    
    /* If idle is running and work is queued, switch immediately. */
    if (current == &idle_proc && have_runnable)
    {
        schedule();
        return;
    }

    /* Check if preemption needed */
    if (current && current != &idle_proc)
    {
        uint64_t now = timer_get_ms();
        uint64_t runtime = now - current->last_run;
        
        /* Time slice expired? */
        if (runtime >= SCHED_QUANTUM_MS)
        {
            schedule();
        }
    }
}

/* ============================
   Block / Unblock
   ============================ */

void sched_block(void)
{
    if (current)
    {
        current->state = PROC_BLOCKED;
        sched_remove(current);
        stats.blocked_count++;
    }
    schedule();
}

void sched_unblock(process_t* p)
{
    if (!p)
        return;
    
    if (p->state == PROC_BLOCKED)
    {
        p->state = PROC_READY;
        sched_add(p);
        
        if (stats.blocked_count > 0)
            stats.blocked_count--;
    }
}

/* ============================
   Priority Management
   ============================ */

void sched_set_priority(process_t* p, int priority)
{
    if (!p)
        return;
    
    if (priority < SCHED_MIN_PRIORITY)
        priority = SCHED_MIN_PRIORITY;
    if (priority > SCHED_MAX_PRIORITY)
        priority = SCHED_MAX_PRIORITY;
    
    p->base_priority = priority;
    p->current_priority = priority;
}

void sched_set_nice(process_t* p, int nice)
{
    if (!p)
        return;
    
    if (nice < -20)
        nice = -20;
    if (nice > 19)
        nice = 19;
    
    p->nice = nice;
    
    /* Adjust current priority based on nice */
    p->current_priority = p->base_priority - nice;
    
    if (p->current_priority < SCHED_MIN_PRIORITY)
        p->current_priority = SCHED_MIN_PRIORITY;
    if (p->current_priority > SCHED_MAX_PRIORITY)
        p->current_priority = SCHED_MAX_PRIORITY;
}

/* ============================
   Idle Process Access
   ============================ */

process_t* sched_get_idle(void)
{
    return &idle_proc;
}

/* ============================
   Statistics
   ============================ */

void sched_get_stats(sched_stats_t* out)
{
    if (!out)
        return;
    
    lock_sched();
    memcpy(out, &stats, sizeof(sched_stats_t));
    unlock_sched();
}

void sched_reset_stats(void)
{
    lock_sched();
    stats.context_switches = 0;
    stats.idle_time = 0;
    stats.total_ticks = 0;
    unlock_sched();
}

/* ============================
   Preemption Control
   ============================ */

void sched_disable_preempt(void)
{
    __sync_fetch_and_add(&preempt_count, 1);
}

void sched_enable_preempt(void)
{
    __sync_fetch_and_sub(&preempt_count, 1);
}

int sched_preempt_enabled(void)
{
    return preempt_count == 0;
}

/* ============================
   Wait Support
   ============================ */

void sched_wake_parent(pid_t child_pid)
{
    /* Find parent of child */
    process_t* child = proc_find(child_pid);
    if (!child)
        return;
    
    process_t* parent = proc_find(child->parent);
    if (!parent)
        return;
    
    /* If parent is waiting for this child, wake it */
    if (parent->state == PROC_WAIT)
    {
        if (parent->wait.waiting_for == child_pid || 
            parent->wait.waiting_for == (pid_t)-1)
        {
            sched_unblock(parent);
        }
    }
}

void sched_sleep(void)
{
    if (current)
    {
        current->state = PROC_WAIT;
        sched_remove(current);
    }
    schedule();
}
