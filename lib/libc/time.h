#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#include "int.h"

// Time types
typedef int64_t time_t;
typedef int32_t suseconds_t;
typedef int32_t clockid_t;

// Clock IDs
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

// Time structures
struct timespec {
    int64_t tv_sec;   // Seconds since epoch
    int32_t tv_nsec;  // Nanoseconds
};

struct timeval {
    time_t      tv_sec;   // Seconds
    suseconds_t tv_usec;  // Microseconds
};

// Calendar time structure
struct tm {
    int tm_sec;    // Seconds [0-59]
    int tm_min;    // Minutes [0-59]
    int tm_hour;   // Hours [0-23]
    int tm_mday;   // Day of month [1-31]
    int tm_mon;    // Month [0-11]
    int tm_year;   // Years since 1900
    int tm_wday;   // Day of week [0-6] (Sunday = 0)
    int tm_yday;   // Day of year [0-365]
    int tm_isdst;  // Daylight saving time flag
};

// Time functions
time_t time(time_t *t);
int clock_gettime(clockid_t clk, struct timespec *ts);
int gettimeofday(struct timeval *tv, void *tz);
int settimeofday(const struct timespec *ts);

// Calendar conversion
struct tm *gmtime(const time_t *timep);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);

// Formatting
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
int format_time(const struct tm *tm, const char *fmt, char *out, size_t max);

// Time profile management
int load_time_profile(void);
int get_time_offset(void);
const char *get_time_format(void);

// Utility
void sleep(unsigned int seconds);
void usleep(unsigned int useconds);

#endif // LIBC_TIME_H
