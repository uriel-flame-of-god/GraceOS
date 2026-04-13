// ============================
// GraceOS Process Security
// Validation and doas enforcement
// ============================

#include "proc.h"
#include "sched.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   Security Configuration
   ============================ */

#define DOAS_CONFIG_PATH    "/etc/doas.conf"

/* ============================
   Authentication Result
   ============================ */

typedef enum auth_result {
    AUTH_OK,
    AUTH_DENIED,
    AUTH_INVALID_USER,
    AUTH_INVALID_PASSWORD,
    AUTH_CONFIG_ERROR
} auth_result_t;

/* ============================
   doas Rule Structure
   ============================ */

typedef struct doas_rule {
    uid_t uid;              /* User who can run doas */
    uid_t target_uid;       /* Target user (usually 0 = root) */
    uint32_t allowed_caps;  /* Capabilities allowed */
    int nopass;             /* Allow without password */
    char cmd[64];           /* Specific command (or "*" for any) */
} doas_rule_t;

/* ============================
   doas Rules Table (in-memory)
   
   In production, loaded from config file.
   ============================ */

#define MAX_DOAS_RULES 64

static doas_rule_t doas_rules[MAX_DOAS_RULES];
static int doas_rule_count = 0;

/* ============================
   Process Validation
   ============================ */

/* Validate a process is safe to run */
int proc_validate(process_t* p)
{
    if (!p)
        return 0;
    
    /* Kernel processes are always valid */
    if (p->flags & PROC_FLAG_KERNEL)
        return 1;
    
    /* Check if process has valid memory */
    if (p->pml4 == 0 && !(p->flags & PROC_FLAG_KERNEL))
    {
        klog_warn("security: Process has no page table");
        return 0;
    }
    
    /* Check if process has at least code segment */
    int has_code = 0;
    for (int i = 0; i < PROC_MAX_SEGMENTS; i++)
    {
        if (p->segments[i] != INVALID_HANDLE)
        {
            has_code = 1;
            break;
        }
    }
    
    if (!has_code && p->state != PROC_NEW)
    {
        klog_warn("security: Process has no segments");
        return 0;
    }
    
    return 1;
}

/* ============================
   Process Invalidation
   ============================ */

void proc_invalidate(process_t* p)
{
    if (!p)
        return;
    
    klog_warn("security: Process invalidated");
    
    p->valid = 0;
    p->state = PROC_BLOCKED;
    sched_remove(p);
}

/* ============================
   Capability Checking
   ============================ */

/* Check if process has a capability */
int proc_has_cap(process_t* p, uint32_t cap)
{
    if (!p)
        return 0;
    
    /* Root has all capabilities */
    if (p->euid == 0)
        return 1;
    
    return (p->caps & cap) != 0;
}

/* Grant capability to process */
int proc_grant_cap(process_t* p, uint32_t cap)
{
    process_t* caller = current;
    
    /* Must have CAP_ADMIN to grant caps */
    if (!proc_has_cap(caller, CAP_ADMIN))
        return -1;
    
    p->caps |= cap;
    return 0;
}

/* Revoke capability from process */
int proc_revoke_cap(process_t* p, uint32_t cap)
{
    process_t* caller = current;
    
    /* Must have CAP_ADMIN to revoke caps */
    if (!proc_has_cap(caller, CAP_ADMIN))
        return -1;
    
    p->caps &= ~cap;
    return 0;
}

/* ============================
   doas Authentication
   ============================ */

/* Check if doas is permitted for user/command */
static auth_result_t doas_check_rules(uid_t uid, uid_t target, const char* cmd)
{
    for (int i = 0; i < doas_rule_count; i++)
    {
        doas_rule_t* rule = &doas_rules[i];
        
        /* Check user */
        if (rule->uid != uid && rule->uid != (uid_t)-1)
            continue;
        
        /* Check target */
        if (rule->target_uid != target && rule->target_uid != (uid_t)-1)
            continue;
        
        /* Check command */
        if (rule->cmd[0] != '*')
        {
            if (strcmp(rule->cmd, cmd) != 0)
                continue;
        }
        
        /* Rule matches */
        return AUTH_OK;
    }
    
    return AUTH_DENIED;
}

/* ============================
   doas Execution Hook
   ============================ */

int doas_execute(process_t* p, uid_t target_uid, const char* cmd, char** argv)
{
    if (!p)
        return -1;
    
    /* Check if doas is allowed */
    auth_result_t auth = doas_check_rules(p->uid, target_uid, cmd);
    
    if (auth != AUTH_OK)
    {
        klog_warn("security: doas denied");
        proc_invalidate(p);
        return -1;
    }
    
    /* Elevate privileges */
    p->euid = target_uid;
    p->egid = 0;  /* Assume root group */
    
    /* Grant elevated capabilities */
    if (target_uid == 0)
    {
        p->caps = CAP_ALL;
    }
    
    /* Set elevated flag */
    p->flags |= PROC_FLAG_ELEVATED;
    
    /* Execute the command */
    /* Note: In real implementation, this would call proc_exec */
    (void)argv;
    
    return 0;
}

/* ============================
   Privilege Drop
   ============================ */

/* Drop back to original UID */
int proc_drop_privileges(process_t* p)
{
    if (!p)
        return -1;
    
    /* Restore original credentials */
    p->euid = p->uid;
    p->egid = p->gid;
    
    /* Clear elevated flag */
    p->flags &= ~PROC_FLAG_ELEVATED;
    
    /* Clear capabilities (except basic ones) */
    p->caps = CAP_NONE;
    
    return 0;
}

/* ============================
   UID/GID Operations
   ============================ */

/* Set UID (requires CAP_SETUID) */
int proc_setuid(process_t* p, uid_t uid)
{
    if (!p)
        return -1;
    
    /* Check capability */
    if (!proc_has_cap(p, CAP_SETUID))
        return -1;
    
    p->uid = uid;
    p->euid = uid;
    
    return 0;
}

/* Set GID (requires CAP_SETUID) */
int proc_setgid(process_t* p, gid_t gid)
{
    if (!p)
        return -1;
    
    /* Check capability */
    if (!proc_has_cap(p, CAP_SETUID))
        return -1;
    
    p->gid = gid;
    p->egid = gid;
    
    return 0;
}

/* ============================
   doas Configuration
   ============================ */

/* Add a doas rule (during system init) */
int doas_add_rule(uid_t uid, uid_t target, const char* cmd, int nopass)
{
    if (doas_rule_count >= MAX_DOAS_RULES)
        return -1;
    
    doas_rule_t* rule = &doas_rules[doas_rule_count++];
    
    rule->uid = uid;
    rule->target_uid = target;
    rule->nopass = nopass;
    rule->allowed_caps = CAP_ALL;
    
    if (cmd)
    {
        strncpy(rule->cmd, cmd, 63);
        rule->cmd[63] = '\0';
    }
    else
    {
        rule->cmd[0] = '*';
        rule->cmd[1] = '\0';
    }
    
    return 0;
}

/* ============================
   Security Initialization
   ============================ */

void security_init(void)
{
    memset(doas_rules, 0, sizeof(doas_rules));
    doas_rule_count = 0;
    
    /* Default rule: root can doas to anyone */
    doas_add_rule(0, (uid_t)-1, NULL, 1);
    
    /* TODO: Load rules from config file */
    
    klog_init_msg("Security subsystem initialized");
}
