// ============================
// GraceOS minit — Service Tree
// Node allocation and tree structure
// ============================

#include "minit.h"
#include "../../log/klog.h"
#include "../../mm/kheap.h"
#include "../../../lib/libc/string.h"

/* ============================
   Node Pool
   ============================ */

static minit_node_t node_pool[MINIT_MAX_NODES];
static int          node_count = 0;

minit_node_t* minit_root = NULL;

/* ============================
   Node Allocation
   ============================ */

static minit_node_t* node_alloc(void)
{
    if (node_count >= MINIT_MAX_NODES)
    {
        klog_warn("minit: node pool exhausted");
        return NULL;
    }
    minit_node_t* n = &node_pool[node_count++];
    memset(n, 0, sizeof(*n));
    return n;
}

/* ============================
   Register
   ============================ */

minit_node_t* minit_register(const char* name, minit_node_t* parent)
{
    if (!name)
        return NULL;

    minit_node_t* n = node_alloc();
    if (!n)
        return NULL;

    strncpy(n->name, name, MINIT_NAME_MAX - 1);
    n->name[MINIT_NAME_MAX - 1] = '\0';

    n->max_restarts     = MINIT_DEFAULT_RESTARTS;
    n->auto_restart     = true;
    n->base_priority    = 0;
    n->effective_priority = 0;

    if (parent)
    {
        n->parent = parent;
        if (parent->child_count < MINIT_MAX_CHILDREN)
            parent->children[parent->child_count++] = n;
        else
            klog_warn("minit: parent node child slots full");
    }
    else
    {
        n->parent = NULL;
    }

    return n;
}

/* ============================
   Attach PCB
   ============================ */

void minit_attach(minit_node_t* node, process_t* proc)
{
    if (!node || !proc)
        return;

    node->proc = proc;

    /* Influence scheduler priority if the proc is not yet scheduled */
    if (node->effective_priority != 0)
        proc->base_priority = node->effective_priority;
}

/* ============================
   Lookup
   ============================ */

minit_node_t* minit_find_by_name(const char* name)
{
    if (!name)
        return NULL;

    for (int i = 0; i < node_count; i++)
    {
        if (strcmp(node_pool[i].name, name) == 0)
            return &node_pool[i];
    }
    return NULL;
}

minit_node_t* minit_find_by_proc(process_t* proc)
{
    if (!proc)
        return NULL;

    for (int i = 0; i < node_count; i++)
    {
        if (node_pool[i].proc == proc)
            return &node_pool[i];
    }
    return NULL;
}

/* ============================
   Debug Dump
   ============================ */

static void dump_node(minit_node_t* n, int depth)
{
    if (!n)
        return;

    char indent[32];
    int d = depth < 10 ? depth : 10;
    for (int i = 0; i < d * 2 && i < 30; i++)
        indent[i] = ' ';
    indent[d * 2] = '\0';

    klog_log(indent);
    klog_log(n->name);

    for (int i = 0; i < n->child_count; i++)
        dump_node(n->children[i], depth + 1);
}

void minit_dump(void)
{
    klog_log("minit: service tree:");
    dump_node(minit_root, 0);
}
