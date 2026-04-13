// ============================
// GraceOS Security Policy Manager (spm)
// Capability-based permission enforcement
// Lumina Kernel subsystem
// ============================

#ifndef GRACEOS_SPM_H
#define GRACEOS_SPM_H

#include "../../lib/libc/int.h"

/* ============================
   Permission Bits
   ============================ */

#define PERM_READ    0x01   /* Read access */
#define PERM_WRITE   0x02   /* Write access */
#define PERM_EXEC    0x04   /* Execute access */
#define PERM_SPAWN   0x08   /* Spawn processes */
#define PERM_KILL    0x10   /* Kill processes */
#define PERM_PAUSE   0x20   /* Pause/resume processes */
#define PERM_GRANT   0x40   /* Grant capabilities to others */
#define PERM_ADMIN   0x80   /* Full administrative access */

#define PERM_ALL     0xFF

// Target pattern examples:
//   "*"          = all resources
//   "/dev/hda"   = specific block device
//   "/dev/"      = any device (prefix)
//   "self"       = process own resources
//   "children"   = child processes
//   "uid:<id>"   = target user
//   "proc:<pid>" = target process

#define SPM_TARGET_MAX  256
#define SPM_NAME_MAX    64
#define SPM_PASS_MAX    64
#define SPM_MAX_USERS   64
#define SPM_MAX_CAPS    256

/* ============================
   User Entry
   ============================ */

typedef struct spm_user {
    uint32_t uid;
    char name[SPM_NAME_MAX];
   char password[SPM_PASS_MAX];
    int active;
} spm_user_t;

/* ============================
   Capability Entry
   ============================ */

typedef struct spm_capability {
    uint32_t uid;
    uint32_t perm;              /* Permission bits this entry grants */
    char target[SPM_TARGET_MAX];
    int active;
} spm_capability_t;

/* ============================
   spm API
   ============================ */

/* Initialize the security subsystem (call before proc_init) */
void spm_init(void);

/* User management */
int spm_add_user(uint32_t uid, const char* name);
spm_user_t* spm_find_user(uint32_t uid);
int spm_user_count(void);
int spm_get_user(int index, spm_user_t* out);
int spm_set_password(uint32_t uid, const char* password);
int spm_auth_user(uint32_t uid, const char* password);

/* Capability management */
int spm_grant_cap(uint32_t uid, uint32_t perm, const char* target);
int spm_revoke_cap(uint32_t uid, uint32_t perm, const char* target);
int spm_cap_count(void);
int spm_get_cap(int index, spm_capability_t* out);

/* Permission check — returns 0 if allowed, -1 if denied */
int spm_check(uint32_t uid, uint32_t perm, const char* target);

/* Audit — called automatically on denial */
void spm_audit(uint32_t uid, uint32_t perm, const char* target, int result);

#endif /* GRACEOS_SPM_H */
