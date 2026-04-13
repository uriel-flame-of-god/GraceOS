#ifndef GRACEOS_SYSINFO_H
#define GRACEOS_SYSINFO_H

#include "../../lib/libc/int.h"

/* ============================
   System Information
   ============================ */

/* CPU Information */
struct cpu_info {
    char vendor[13];
    char brand[49];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    int has_apic;
    int has_msr;
    int has_sse;
    int has_sse2;
    int has_avx;
};

/* Memory Information */
struct memory_info {
    uint64_t total_kb;
    uint64_t used_kb;
    uint64_t free_kb;
};

/* System Information */
struct system_info {
    const char* os_name;
    const char* os_version;
    const char* kernel_version;
    const char* architecture;
    uint64_t uptime_seconds;
};

/* API Functions */
void sysinfo_init(void);
void sysinfo_get_cpu(struct cpu_info* info);
void sysinfo_get_memory(struct memory_info* info);
void sysinfo_get_system(struct system_info* info);
void sysinfo_display(void);

#endif /* GRACEOS_SYSINFO_H */
