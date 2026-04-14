// ============================
// GraceOS minit — Orchestration
// Boot sequence, service lifecycle, init root node
// ============================

#include "minit.h"
#include "../../log/klog.h"
#include "../sched.h"
#include "../../mm/kheap.h"

/* Current process pointer — defined in proc.c */
extern process_t* current;

#define MINIT_STACK_POOL_SIZE 8
#define MINIT_STACK_SIZE      PROC_STACK_SIZE

static uint8_t minit_stack_pool[MINIT_STACK_POOL_SIZE][MINIT_STACK_SIZE]
    __attribute__((aligned(16)));
static uint8_t minit_stack_used[MINIT_STACK_POOL_SIZE];

static void* minit_stack_alloc(void)
{
    for (int i = 0; i < MINIT_STACK_POOL_SIZE; i++)
    {
        if (!minit_stack_used[i])
        {
            minit_stack_used[i] = 1;
            return minit_stack_pool[i];
        }
    }
    return NULL;
}

static void minit_kernel_trampoline(void)
{
    /* A freshly-spawned process starts here instead of returning from a
       ctx_switch() inside schedule().  schedule() called sched_disable_preempt()
       before switching to us but its matching sched_enable_preempt() (after
       ctx_switch) never ran.  Re-enable now so the timer can preempt this
       process and schedule others. */
    sched_enable_preempt();

    minit_node_t* node = minit_find_by_proc(current);

    if (!node || !node->entry_fn)
    {
        klog_warn("minit: kernel service has no entry");
    }
    else
    {
        node->entry_fn();
    }

    if (current)
        proc_set_zombie(current, 0);

    schedule();

    for (;;)
        __asm__ volatile ("hlt");
}

/* ============================
   Initialization
   ============================ */

void minit_init(void)
{
    /* Build the root node that represents the system service tree */
    minit_root = minit_register("system", NULL);

    if (!minit_root)
    {
        klog_warn("minit: failed to create root node");
        return;
    }

    klog_init_msg("minit: process orchestration layer initialized");
}

/* ============================
   Spawn
   ============================ */

int minit_spawn(minit_node_t* node)
{
    if (!node || node->disabled)
        return -1;

    if (!node->entry_fn)
    {
        klog_warn("minit: spawn called with no entry_fn");
        return -1;
    }

     sched_disable_preempt();

     /* Disable interrupts around proc_create.
         proc_alloc_pid holds proc_lock (a spinlock). The timer ISR calls
         proc_check_sleepers which tries to acquire the same lock. If the
         IRQ fires while we hold the lock, the ISR spins with interrupts
         disabled → deadlock. cli here prevents that window. */
     uint64_t irq_flags = 0;
     __asm__ volatile ("pushfq; pop %0" : "=r"(irq_flags));
     __asm__ volatile ("cli");
     process_t* p = proc_create(NULL);
     if (irq_flags & (1ULL << 9))
          __asm__ volatile ("sti");

    if (!p)
    {
        klog_warn("minit: proc_create failed");
        sched_enable_preempt();
        return -1;
    }

    /* Mark as kernel-mode service */
    p->flags |= PROC_FLAG_KERNEL;

    proc_set_name(p, node->name);

    /* Apply minit priority */
    if (node->base_priority != 0)
    {
        p->base_priority      = node->base_priority;
        p->current_priority   = node->base_priority;
    }

    /* Attach PCB to the node */
    minit_attach(node, p);

    /* Allocate a kernel stack and initialize a proper context. */
    void* stack = kmalloc(PROC_STACK_SIZE);
    if (!stack)
    {
        stack = minit_stack_alloc();
        if (!stack)
        {
            klog_warn("minit: kernel stack alloc failed");
            sched_enable_preempt();
            return -1;
        }
        klog_warn("minit: using fallback kernel stack");
    }

    p->kernel_stack_base = (uint64_t)(uintptr_t)stack;
    p->kernel_stack_size = PROC_STACK_SIZE;

    ctx_init_kernel(&p->ctx, minit_kernel_trampoline,
                    (uint8_t*)stack + PROC_STACK_SIZE);

    klog_logn("minit: spawning service");
    klog_logn(node->name);

    proc_set_ready(p);
    sched_enable_preempt();
    return 0;
}
