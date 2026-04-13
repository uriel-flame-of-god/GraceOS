#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include "../../lib/libc/int.h"

// Time types
typedef int64_t time_t;
typedef int32_t suseconds_t;
typedef int32_t clockid_t;

// Clock IDs
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

// Primary time structure (nanosecond precision)
struct timespec {
    int64_t tv_sec;   // Seconds since epoch (UTC)
    int32_t tv_nsec;  // Nanoseconds
};

// Legacy time structure (microsecond precision)
struct timeval {
    time_t      tv_sec;   // Seconds
    suseconds_t tv_usec;  // Microseconds
};

// Global time variables (defined in kernel/core/time.c)
extern struct timespec system_time;      // System realtime clock (UTC)
extern struct timespec monotonic_time;   // Monotonic clock

// Timer configuration
#define TIMER_FREQ_HZ 1000              // 1000 Hz = 1 ms tick
#define TICK_NS       1000000           // Nanoseconds per tick

// Kernel time functions
void time_init(void);
void timer_tick(void);
uint64_t timer_get_ms(void);
time_t get_time_seconds(void);
void get_time(struct timespec *ts);
void get_monotonic(struct timespec *ts);
int set_time(const struct timespec *ts);
void rtc_init_time(int year, int month, int day, int hour, int minute, int second);

// Calendar time conversion helpers
int is_leap_year(int year);
int days_in_month(int month, int year);
int days_since_epoch(int year, int month, int day);

#endif // KERNEL_TIME_H
