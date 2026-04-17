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

/* ============================
   Kernel Log Ring Buffer
   Used by the background logger service to drain and persist logs.
   ============================ */

/* Size of the in-memory ring buffer (128 KB — holds ~5 s of typical log traffic) */
#define KLOG_RING_SIZE (128 * 1024)

/*
 * Read new log entries from the ring buffer.
 *   *pos     – caller-maintained read position (monotonically increasing).
 *              On first call pass 0; on each subsequent call pass the value
 *              returned through *pos by the previous call.
 *   buf      – destination buffer (NUL-terminated on return).
 *   size     – capacity of buf in bytes (including the NUL terminator).
 * Returns the number of bytes copied (0 = no new data).
 * If the caller has fallen more than KLOG_RING_SIZE bytes behind, *pos is
 * advanced to the oldest available entry automatically.
 */
int klog_read_new(uint64_t* pos, char* buf, int size);

/* Return the current monotonic write position in the ring buffer. */
uint64_t klog_get_write_pos(void);

#endif /* GRACEOS_KLOG_H */
