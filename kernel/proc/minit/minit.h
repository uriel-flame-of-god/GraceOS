// ============================
// GraceOS minit — Process Orchestration Layer
// Logical service tree over the Lumina process manager
// Placement: kernel/proc/minit/
// ============================

#ifndef GRACEOS_MINIT_H
#define GRACEOS_MINIT_H

#include "../proc.h"

/* ============================
   Limits
   ============================ */

#define MINIT_NAME_MAX       64
#define MINIT_MAX_NODES      64
#define MINIT_MAX_CHILDREN   16
#define MINIT_DEFAULT_RESTARTS 3

/* ============================
   minit Node

   Logical service descriptor.
   Each node may reference a PCB (proc) while that PCB is running.
   The node outlives the PCB and handles restart/exhaustion logic.
   ============================ */

typedef struct minit_node {
    char name[MINIT_NAME_MAX];

    /* Linked execution unit — NULL when service is not running */
    process_t* proc;

    /* Tree structure */
    struct minit_node* parent;
    struct minit_node* children[MINIT_MAX_CHILDREN];
    int                child_count;

    /* Scheduling influence */
    int base_priority;
    int effective_priority;

    /* Restart policy */
    int max_restarts;
    int crash_count;
    bool auto_restart;
    bool disabled;          /* Set when exhausted — do not restart */

    /* Kernel-mode entry point (NULL for external exec-based services) */
    void (*entry_fn)(void);
} minit_node_t;

/* ============================
   Global root node
   ============================ */

extern minit_node_t* minit_root;

/* ============================
   API
   ============================ */

/* Initialize the minit layer (call after proc_init) */
void minit_init(void);

/* Spawn a registered service node:
   - Creates a PCB via proc_create
   - Attaches it to the node
   - Adds it to the scheduler
   - Calls node->entry_fn() (for kernel-mode services)
   Returns 0 on success, -1 on failure. */
int minit_spawn(minit_node_t* node);

/* Register a named service node under parent (NULL = root) */
minit_node_t* minit_register(const char* name, minit_node_t* parent);

/* Attach a live PCB to a node */
void minit_attach(minit_node_t* node, process_t* proc);

/* Notify minit that a process has crashed.
   Handles restart or exhaustion depending on crash_count. */
void minit_notify_crash(process_t* proc);

/* Lookup by name */
minit_node_t* minit_find_by_name(const char* name);

/* Lookup by PCB pointer */
minit_node_t* minit_find_by_proc(process_t* proc);

/* Print the service tree (debug) */
void minit_dump(void);

#endif /* GRACEOS_MINIT_H */
