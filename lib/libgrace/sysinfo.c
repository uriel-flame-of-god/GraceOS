// ============================
// GraceOS Sysinfo Userland Wrapper
// ============================

#include "grace.h"
#include "../../include/grace/sysinfo.h"

/* ============================
   Sysinfo Syscall Wrapper
   ============================ */

int grace_sysinfo(struct grace_sysinfo* info)
{
    return (int)__syscall1(SYS_SYSINFO, (long)info);
}
