// ============================
// GraceOS SPM Syscalls
// Userland interface to the security policy manager
// ============================

#include "../include/syscall.h"
#include "../spm/spm.h"
#include "../proc/proc.h"
#include "../../lib/libc/string.h"
#include "../../include/grace/spm_syscalls.h"

long sys_spm_user_add(long uid, long name_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    if (!current || !name_ptr)
        return -1;

    if (spm_check((uint32_t)current->uid, PERM_ADMIN, "*") < 0)
        return -1;

    return (long)spm_add_user((uint32_t)uid, (const char*)name_ptr);
}

long sys_spm_user_enum(long index, long out_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    if (!out_ptr)
        return -1;

    spm_user_t user;
    if (spm_get_user((int)index, &user) < 0)
        return -1;

    spm_user_info_t info;
    info.uid = user.uid;
    info.active = user.active;
    strncpy(info.name, user.name, SPM_NAME_MAX - 1);
    info.name[SPM_NAME_MAX - 1] = '\0';

    memcpy((void*)out_ptr, &info, sizeof(info));
    return 0;
}

long sys_spm_cap_grant(long uid, long perm, long target_ptr, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;

    if (!current || !target_ptr)
        return -1;

    if (spm_check((uint32_t)current->uid, PERM_GRANT, (const char*)target_ptr) < 0)
        return -1;

    return (long)spm_grant_cap((uint32_t)uid, (uint32_t)perm, (const char*)target_ptr);
}

long sys_spm_cap_enum(long index, long out_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    if (!out_ptr)
        return -1;

    spm_capability_t cap;
    if (spm_get_cap((int)index, &cap) < 0)
        return -1;

    spm_cap_info_t info;
    info.uid = cap.uid;
    info.perm = cap.perm;
    info.active = cap.active;
    strncpy(info.target, cap.target, SPM_TARGET_MAX - 1);
    info.target[SPM_TARGET_MAX - 1] = '\0';

    memcpy((void*)out_ptr, &info, sizeof(info));
    return 0;
}

long sys_spm_check(long uid, long perm, long target_ptr, long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;

    if (!current || !target_ptr)
        return -1;

    uint32_t caller_uid = (uint32_t)current->uid;
    uint32_t target_uid = (uint32_t)uid;

    if (caller_uid != target_uid)
    {
        if (spm_check(caller_uid, PERM_ADMIN, "*") < 0)
            return -1;
    }

    return (long)spm_check(target_uid, (uint32_t)perm, (const char*)target_ptr);
}

long sys_spm_user_passwd(long uid, long password_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    if (!current || !password_ptr)
        return -1;

    uint32_t caller_uid = (uint32_t)current->uid;
    uint32_t target_uid = (uint32_t)uid;

    if (caller_uid != target_uid)
    {
        if (spm_check(caller_uid, PERM_ADMIN, "*") < 0)
            return -1;
    }

    return (long)spm_set_password(target_uid, (const char*)password_ptr);
}

long sys_spm_auth(long uid, long password_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    if (!password_ptr)
        return -1;

    return (long)spm_auth_user((uint32_t)uid, (const char*)password_ptr);
}
