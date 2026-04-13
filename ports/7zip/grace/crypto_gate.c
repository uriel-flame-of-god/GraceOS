#include "crypto_gate.h"

#include "grace.h"
#include <grace/spm_syscalls.h>
#include <stdlib.h>

#ifndef GRACEOS_CRYPTO_TARGET
#define GRACEOS_CRYPTO_TARGET "crypto/7zip"
#endif

#ifndef GRACEOS_CRYPTO_PERM
#define GRACEOS_CRYPTO_PERM PERM_EXEC
#endif

#ifndef GRACEOS_CRYPTO_HARD_FAIL
#define GRACEOS_CRYPTO_HARD_FAIL 0
#endif

#if GRACEOS_CRYPTO_HARD_FAIL
__attribute__((noreturn)) void abort(void);
#endif

static int g_checked;
static int g_allowed;

int grace_crypto_permitted(void)
{
    if (!g_checked) {
        uid_t uid = getuid();
        g_allowed = (spm_check_user((uint32_t)uid, GRACEOS_CRYPTO_PERM, GRACEOS_CRYPTO_TARGET) == 0);
        g_checked = 1;
    }

    #if GRACEOS_CRYPTO_HARD_FAIL
    if (!g_allowed) {
        abort();
    }
    #endif

    return g_allowed;
}
