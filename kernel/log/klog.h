// ============================
// Lumina Kernel Logger
// Standardized logging system
// ============================

#ifndef GRACEOS_KLOG_H
#define GRACEOS_KLOG_H

#include "../../lib/libc/int.h"

/* Initialize kernel logging system */
void klog_init(void);

/* Logging functions with standardized tags */
void klog_init_msg(const char* msg);   // [INIT]  - Startup messages
void klog_get(const char* msg);        // [GET]   - Allocation
void klog_set(const char* msg);        // [SET]   - Free / Mark used
void klog_warn(const char* msg);       // [WARN]  - Suspicious state
void klog_error(const char* msg);      // [ERROR] - Recoverable error
void klog_fail(const char* msg);       // [FAIL]  - Fatal failure
void klog_log(const char* msg);        // [LOG]   - Debug info
void klog_logn(const char* msg);       // [LOG]   - Debug info + newline
void klog_hex(uint64_t value);         // Print hex value inline

#endif /* GRACEOS_KLOG_H */
