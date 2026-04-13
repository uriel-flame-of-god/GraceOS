/*
 * Time syscall wrappers for userland
 * Links libc time functions to kernel syscalls
 */

#include "time.h"

// Syscall numbers (should match kernel/include/syscall.h)
#define SYS_TIME           25
#define SYS_CLOCK_GETTIME  26
#define SYS_GETTIMEOFDAY   27
#define SYS_SETTIMEOFDAY   28

/*
 * Syscall interface (implemented in assembly)
 */
extern int64_t __syscall1(int num, int64_t arg1);
extern int64_t __syscall2(int num, int64_t arg1, int64_t arg2);

/*
 * sys_time wrapper
 */
time_t _syscall_time(time_t *t) {
    return (time_t)__syscall1(SYS_TIME, (int64_t)t);
}

/*
 * sys_clock_gettime wrapper
 */
int _syscall_clock_gettime(clockid_t clk, struct timespec *ts) {
    return (int)__syscall2(SYS_CLOCK_GETTIME, (int64_t)clk, (int64_t)ts);
}

/*
 * sys_gettimeofday wrapper
 */
int _syscall_gettimeofday(struct timeval *tv, void *tz) {
    return (int)__syscall2(SYS_GETTIMEOFDAY, (int64_t)tv, (int64_t)tz);
}

/*
 * sys_settimeofday wrapper
 */
int _syscall_settimeofday(const struct timespec *ts) {
    return (int)__syscall1(SYS_SETTIMEOFDAY, (int64_t)ts);
}
