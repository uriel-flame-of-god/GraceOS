// ============================
// GraceOS SPM Syscall ABI
// Shared between kernel and userland
// ============================

#ifndef GRACE_SPM_SYSCALLS_H
#define GRACE_SPM_SYSCALLS_H

#include "../../lib/libc/int.h"

/* ============================
   Permission Bits
   ============================ */

#ifndef PERM_READ
#define PERM_READ    0x01
#define PERM_WRITE   0x02
#define PERM_EXEC    0x04
#define PERM_SPAWN   0x08
#define PERM_KILL    0x10
#define PERM_PAUSE   0x20
#define PERM_GRANT   0x40
#define PERM_ADMIN   0x80
#define PERM_ALL     0xFF
#endif

#ifndef SPM_TARGET_MAX
#define SPM_TARGET_MAX 256
#endif

#ifndef SPM_NAME_MAX
#define SPM_NAME_MAX   64
#endif

/* ============================
   ABI Structures
   ============================ */

typedef struct spm_user_info {
    uint32_t uid;
    char name[SPM_NAME_MAX];
    int active;
} spm_user_info_t;

typedef struct spm_cap_info {
    uint32_t uid;
    uint32_t perm;
    char target[SPM_TARGET_MAX];
    int active;
} spm_cap_info_t;

/* ============================
   Userland API
   ============================ */

int spm_user_add(uint32_t uid, const char* name);
int spm_user_enum(int index, spm_user_info_t* out);
int spm_cap_grant(uint32_t uid, uint32_t perm, const char* target);
int spm_cap_enum(int index, spm_cap_info_t* out);
int spm_check_user(uint32_t uid, uint32_t perm, const char* target);
int spm_user_passwd(uint32_t uid, const char* password);
int spm_user_auth(uint32_t uid, const char* password);

#endif /* GRACE_SPM_SYSCALLS_H */
