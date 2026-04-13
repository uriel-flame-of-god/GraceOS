// ============================
// GraceOS Hybrid Scheduler
// SJF + FCFS + Aging
// ============================

#ifndef GRACEOS_SCHED_H
#define GRACEOS_SCHED_H

#include "proc.h"

/* ============================
   Scheduler Configuration
   ============================ */

#define SCHED_QUANTUM_MS        10      /* Time slice in milliseconds */
#define SCHED_AGING_TICK_MS     1       /* Aging increment interval */
#define SCHED_AGING_FACTOR      100     /* wait_time / AGING_FACTOR */
#define SCHED_INVALID_PENALTY   10000   /* Score penalty for invalid processes */
#define SCHED_MAX_PRIORITY      40      /* Maximum priority value */
#define SCHED_DEFAULT_PRIORITY  20      /* Default priority */
#define SCHED_MIN_PRIORITY      0       /* Minimum priority value */

/* ============================
   Priority Classes (NT-style)
   ============================ */

typedef enum priority_class {
    PRIO_IDLE       = 0,    /* Runs only when nothing else */
    PRIO_BELOW_NORM = 6,    /* Below normal */
    PRIO_NORMAL     = 10,   /* Normal interactive */
    PRIO_ABOVE_NORM = 14,   /* Above normal */
    PRIO_HIGH       = 18,   /* High priority */
    PRIO_REALTIME   = 24    /* Real-time (kernel only) */
} priority_class_t;

/* ============================
   Scheduler Statistics
   ============================ */

typedef struct sched_stats {
    uint64_t context_switches;
    uint64_t idle_time;
    uint64_t total_ticks;
    uint32_t runnable_count;
    uint32_t blocked_count;
    uint32_t zombie_count;
} sched_stats_t;

/* ============================
   Scheduler API
   ============================ */

/* Initialize scheduler */
void sched_init(void);

/* Add process to run queue */
void sched_add(process_t* p);

/* Remove process from run queue */
void sched_remove(process_t* p);

/* Pick next process to run (hybrid algorithm) */
process_t* sched_pick(void);

/* Main scheduling function - pick and switch */
void schedule(void);

/* Yield CPU to another process */
void sched_yield(void);

/* Timer tick handler - call from IRQ */
void sched_tick(void);

/* Block current process */
void sched_block(void);

/* Unblock a process */
void sched_unblock(process_t* p);

/* Set process priority */
void sched_set_priority(process_t* p, int priority);

/* Set nice value */
void sched_set_nice(process_t* p, int nice);

/* ============================
   Score Calculation
   ============================ */

/*
   Hybrid Score Formula:
   
   score = est_runtime - (wait_time / AGING_FACTOR) + invalid_penalty
   
   Lower score = higher priority
   
   - SJF: Shorter estimated runtime gets lower score
   - FCFS: Wait time reduces score (aging)
   - Security: Invalid processes get penalty
*/

int64_t sched_calc_score(process_t* p);

/* ============================
   Idle Process
   ============================ */

/* Get idle process */
process_t* sched_get_idle(void);

/* ============================
   Statistics
   ============================ */

/* Get scheduler statistics */
void sched_get_stats(sched_stats_t* stats);

/* Reset statistics */
void sched_reset_stats(void);

/* ============================
   Preemption Control
   ============================ */

/* Disable preemption */
void sched_disable_preempt(void);

/* Enable preemption */
void sched_enable_preempt(void);

/* Check if preemption is enabled */
int sched_preempt_enabled(void);

/* ============================
   Wait Queue Support
   ============================ */

/* Wake up process waiting for PID */
void sched_wake_parent(pid_t child_pid);

/* Sleep until condition (called by waitpid) */
void sched_sleep(void);

#endif /* GRACEOS_SCHED_H */
