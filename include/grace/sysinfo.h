// ============================
// GraceOS Sysinfo ABI Header
// Shared between kernel and userland
// ============================

#ifndef GRACE_SYSINFO_H
#define GRACE_SYSINFO_H

/* ============================
   Type Definitions (standalone)
   ============================ */

#ifndef __UINT_TYPES_DEFINED
#define __UINT_TYPES_DEFINED
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
#endif

/* ============================
   Sysinfo Version
   ============================ */

#define GRACE_SYSINFO_VERSION 1

/* ============================
   Sysinfo Structure
   
   WARNING: ABI STABLE!
   - Never reorder fields
   - Never change field sizes
   - Only append at end
   ============================ */

struct grace_sysinfo {
    uint32_t version;           // Structure version

    uint64_t uptime_ms;         // System uptime in milliseconds

    uint64_t total_mem;         // Total memory in bytes
    uint64_t free_mem;          // Free memory in bytes

    uint32_t process_count;     // Number of running processes

    char kernel_name[32];       // Kernel name (e.g., "GraceKernel")
    char kernel_version[16];    // Kernel version (e.g., "0.1")

    char arch[16];              // Architecture (e.g., "x86_64")
    char fs_name[16];           // Filesystem name (e.g., "BranchFS")
};

/* ============================
   Userland API
   ============================ */

int grace_sysinfo(struct grace_sysinfo* info);

#endif /* GRACE_SYSINFO_H */
