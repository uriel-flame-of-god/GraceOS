// ============================
// GraceOS Security Policy Manager (spm)
// Implementation
// ============================

#include "spm.h"
#include "../log/klog.h"
#include "../../lib/libc/string.h"

/* ============================
   Internal State
   ============================ */

static spm_user_t     users[SPM_MAX_USERS];
static int            user_count = 0;

static spm_capability_t caps[SPM_MAX_CAPS];
static int              cap_count = 0;

/* ============================
   Target Pattern Matching
   ============================ */

/* Check if 'pattern' covers 'target'.
   Supports trailing wildcard: "/dev/" prefix, or bare "*". */
static int target_matches(const char* pattern, const char* target)
{
    if (!pattern || !target)
        return 0;

    /* Exact wildcard */
    if (pattern[0] == '*' && pattern[1] == '\0')
        return 1;

    size_t plen = strlen(pattern);

    /* Trailing wildcard: pattern ending in slash-star matches any path under it */
    if (plen >= 2 && pattern[plen - 1] == '*' && pattern[plen - 2] == '/')
    {
        /* Check prefix up to and including the '/' */
        size_t prefix_len = plen - 1;
        if (strlen(target) >= prefix_len &&
            strncmp(pattern, target, prefix_len) == 0)
            return 1;
        return 0;
    }

    /* Exact match */
    return strcmp(pattern, target) == 0;
}

/* ============================
   Initialization
   ============================ */

void spm_init(void)
{
    memset(users, 0, sizeof(users));
    memset(caps,  0, sizeof(caps));
    user_count = 0;
    cap_count  = 0;

    /* Register root user */
    spm_add_user(0, "root");

    /* Root has full permissions on everything */
    spm_grant_cap(0, PERM_ALL, "*");

    klog_init_msg("spm: security policy manager initialized");
}

/* ============================
   User Management
   ============================ */

int spm_add_user(uint32_t uid, const char* name)
{
    if (!name || !*name)
        return -1;

    if (user_count >= SPM_MAX_USERS)
        return -1;

    if (spm_find_user(uid))
        return -1;

    for (int i = 0; i < user_count; i++)
    {
        if (users[i].active && strcmp(users[i].name, name) == 0)
            return -1;
    }

    spm_user_t* u = &users[user_count++];
    u->uid    = uid;
    u->active = 1;

    if (name)
    {
        strncpy(u->name, name, SPM_NAME_MAX - 1);
        u->name[SPM_NAME_MAX - 1] = '\0';
    }
    u->password[0] = '\0';

    return 0;
}

spm_user_t* spm_find_user(uint32_t uid)
{
    for (int i = 0; i < user_count; i++)
    {
        if (users[i].active && users[i].uid == uid)
            return &users[i];
    }
    return NULL;
}

int spm_user_count(void)
{
    return user_count;
}

int spm_get_user(int index, spm_user_t* out)
{
    if (!out || index < 0 || index >= user_count)
        return -1;

    *out = users[index];
    return 0;
}

int spm_set_password(uint32_t uid, const char* password)
{
    if (!password)
        return -1;

    spm_user_t* u = spm_find_user(uid);
    if (!u)
        return -1;

    strncpy(u->password, password, SPM_PASS_MAX - 1);
    u->password[SPM_PASS_MAX - 1] = '\0';
    return 0;
}

int spm_auth_user(uint32_t uid, const char* password)
{
    if (!password)
        return -1;

    spm_user_t* u = spm_find_user(uid);
    if (!u)
        return -1;

    return strcmp(u->password, password) == 0 ? 0 : -1;
}

/* ============================
   Capability Management
   ============================ */

int spm_grant_cap(uint32_t uid, uint32_t perm, const char* target)
{
    if (!target || cap_count >= SPM_MAX_CAPS)
        return -1;

    spm_capability_t* c = &caps[cap_count++];
    c->uid    = uid;
    c->perm   = perm;
    c->active = 1;
    strncpy(c->target, target, SPM_TARGET_MAX - 1);
    c->target[SPM_TARGET_MAX - 1] = '\0';

    return 0;
}

int spm_revoke_cap(uint32_t uid, uint32_t perm, const char* target)
{
    for (int i = 0; i < cap_count; i++)
    {
        spm_capability_t* c = &caps[i];
        if (!c->active)
            continue;
        if (c->uid != uid)
            continue;
        if (!target_matches(c->target, target))
            continue;

        c->perm &= ~perm;
        if (c->perm == 0)
            c->active = 0;

        return 0;
    }
    return -1;
}

int spm_cap_count(void)
{
    return cap_count;
}

int spm_get_cap(int index, spm_capability_t* out)
{
    if (!out || index < 0 || index >= cap_count)
        return -1;

    *out = caps[index];
    return 0;
}

/* ============================
   Permission Check
   ============================ */

int spm_check(uint32_t uid, uint32_t perm, const char* target)
{
    /* UID 0 (root) is unconditionally allowed */
    if (uid == 0)
        return 0;

    for (int i = 0; i < cap_count; i++)
    {
        spm_capability_t* c = &caps[i];

        if (!c->active)
            continue;
        if (c->uid != uid)
            continue;

        /* All requested permission bits must be present */
        if ((c->perm & perm) != perm)
            continue;

        if (target_matches(c->target, target))
            return 0;
    }

    spm_audit(uid, perm, target, -1);
    return -1;
}

/* ============================
   Audit
   ============================ */

void spm_audit(uint32_t uid, uint32_t perm, const char* target, int result)
{
    (void)uid; (void)perm; (void)target;

    if (result < 0)
        klog_warn("spm: permission denied");
}
