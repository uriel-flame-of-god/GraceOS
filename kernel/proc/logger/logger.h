// ============================
// GraceOS Unified Logger Service
// Background kernel-mode process managed by minit.
// Drains the klog ring buffer every 5 seconds and appends
// timestamped batches to system/log.txt in BranchFS.
// ============================

#ifndef GRACEOS_LOGGER_H
#define GRACEOS_LOGGER_H

/*
 * logger_service_main — entry function registered with minit.
 *
 * minit registers this as a node with auto_restart = true so that
 * if the logger crashes, minit re-spawns it (up to max_restarts).
 *
 * Memory:
 *   Two SASY segments are allocated with owner_pid = 0 (kernel) on
 *   first run and reused on restart, so state and the accumulation
 *   buffer survive across minit-initiated restarts.
 *
 * Sleep:
 *   The service sets current->sleep_until and calls sched_block().
 *   proc_check_sleepers() (called by the timer ISR at 1000 Hz) wakes
 *   it after LOGGER_INTERVAL_MS milliseconds.
 */
void logger_service_main(void);

#endif /* GRACEOS_LOGGER_H */
