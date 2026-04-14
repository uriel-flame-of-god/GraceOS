// ============================
// GraceOS minit — Exhaustion & Crash Handling
// Tracks crash counts and disables exhausted services
// ============================

#include "minit.h"
#include "../../log/klog.h"

/* ============================
   Crash Notification
   ============================ */

void minit_notify_crash(process_t* proc)
{
    if (!proc)
        return;

    minit_node_t* node = minit_find_by_proc(proc);
    if (!node)
    {
        /* Process not tracked by minit — nothing to do */
        return;
    }

    /* Detach the dead PCB */
    node->proc = NULL;

    if (!node->auto_restart || node->disabled)
        return;

    node->crash_count++;

    if (node->crash_count > node->max_restarts)
    {
        /* Exhausted — disable this service permanently */
        node->disabled = true;
        klog_warn("minit: service exhausted, disabled");
        klog_warn(node->name);
        return;
    }

    /* Signal that this node needs restart.
       Actual respawn is done by orchestration.c when the scheduler
       next runs — we only mark the intent here. */
    klog_log("minit: crash noted, restart pending");
}
