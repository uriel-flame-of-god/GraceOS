/*
 * Time-related system calls
 */

#include "../include/time.h"
#include "../proc/proc.h"

/*
 * sys_time - Get current time in seconds
 * 
 * Returns: Current UTC time in seconds since epoch
 */
long sys_time(long t_ptr, long unused1, long unused2, long unused3, long unused4, long unused5) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    time_t current = get_time_seconds();
    
    if (t_ptr) {
        // TODO: Validate user pointer
        time_t *t = (time_t *)t_ptr;
        *t = current;
    }
    
    return (long)current;
}

/*
 * sys_time_ms - Get current time in milliseconds
 * 
 * Returns: Current monotonic time in milliseconds
 */
long sys_time_ms(long unused1, long unused2, long unused3, long unused4, long unused5, long unused6) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5; (void)unused6;
    
    return (long)timer_get_ms();
}

/*
 * sys_clock_gettime - Get time with nanosecond precision
 * 
 * @clk_id: Clock ID (CLOCK_REALTIME or CLOCK_MONOTONIC)
 * @ts_ptr: Pointer to timespec structure
 * 
 * Returns: 0 on success, -1 on error
 */
long sys_clock_gettime(long clk_id, long ts_ptr, long unused1, long unused2, long unused3, long unused4) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    
    if (!ts_ptr) {
        return -1;
    }
    
    // TODO: Validate user pointer
    struct timespec *ts = (struct timespec *)ts_ptr;
    clockid_t clk = (clockid_t)clk_id;
    
    switch (clk) {
        case CLOCK_REALTIME:
            get_time(ts);
            return 0;
            
        case CLOCK_MONOTONIC:
            get_monotonic(ts);
            return 0;
            
        default:
            return -1;
    }
}

/*
 * sys_settimeofday - Set system time
 * 
 * @ts_ptr: New time value
 * 
 * Returns: 0 on success, -1 on error
 * 
 * Security: Requires root privileges
 */
long sys_settimeofday(long ts_ptr, long unused1, long unused2, long unused3, long unused4, long unused5) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    if (!ts_ptr) {
        return -1;
    }
    
    // TODO: Validate user pointer
    const struct timespec *ts = (const struct timespec *)ts_ptr;
    
    // Check privileges
    // TODO: Implement proper privilege check
    // For now, assume we need to check if current process is root
    /*
    if (current_process && current_process->uid != 0) {
        return -1;  // Permission denied
    }
    */
    
    return set_time(ts);
}

/*
 * sys_gettimeofday - Legacy system call
 * 
 * @tv_ptr: Timeval structure
 * @tz_ptr: Timezone (ignored, deprecated)
 * 
 * Returns: 0 on success, -1 on error
 */
long sys_gettimeofday(long tv_ptr, long tz_ptr, long unused1, long unused2, long unused3, long unused4) {
    (void)tz_ptr; (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    
    if (!tv_ptr) {
        return -1;
    }
    
    // TODO: Validate user pointer
    struct timeval *tv = (struct timeval *)tv_ptr;
    
    struct timespec ts;
    get_time(&ts);
    
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    
    // Timezone is deprecated and ignored
    
    return 0;
}
