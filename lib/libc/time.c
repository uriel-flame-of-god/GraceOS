/*
 * GraceOS libc - Time Functions
 * POSIX-compatible time API with calendar conversion and formatting
 */

#include "time.h"
#include "string.h"

// Days in each month (non-leap year)
static const int month_days[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

// Month names
static const char *month_names[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

// Day names
static const char *day_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// Thread-local storage for gmtime/localtime
static struct tm _tm_buffer;

// Time profile cache (shared with timeprofile.c)
int profile_offset = 0;  // Offset in seconds
char profile_format[64] = "DD-MM-YYYY HH:mm:ss A";
int profile_loaded = 0;

/*
 * Forward declarations
 */
extern time_t _syscall_time(time_t *t);
extern int _syscall_clock_gettime(clockid_t clk, struct timespec *ts);
extern int _syscall_gettimeofday(struct timeval *tv, void *tz);
extern int _syscall_settimeofday(const struct timespec *ts);

/*
 * Check if year is a leap year
 */
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*
 * Get days in month
 */
static int days_in_month(int month, int year) {
    if (month < 0 || month > 11) {
        return 0;
    }
    
    if (month == 1 && is_leap_year(year)) {
        return 29;
    }
    
    return month_days[month];
}

/*
 * Calculate day of week (0 = Sunday)
 * Using Zeller's congruence
 */
static int day_of_week(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year--;
    }
    
    int q = day;
    int m = month;
    int k = year % 100;
    int j = year / 100;
    
    int h = (q + ((13 * (m + 1)) / 5) + k + (k / 4) + (j / 4) - (2 * j)) % 7;
    
    // Convert to 0=Sunday
    return (h + 6) % 7;
}

/*
 * time() - Get current time in seconds
 */
time_t time(time_t *t) {
    return _syscall_time(t);
}

/*
 * clock_gettime() - Get time with nanosecond precision
 */
int clock_gettime(clockid_t clk, struct timespec *ts) {
    return _syscall_clock_gettime(clk, ts);
}

/*
 * gettimeofday() - Legacy function
 */
int gettimeofday(struct timeval *tv, void *tz) {
    return _syscall_gettimeofday(tv, tz);
}

/*
 * settimeofday() - Set system time (requires root)
 */
int settimeofday(const struct timespec *ts) {
    return _syscall_settimeofday(ts);
}

/*
 * gmtime_r() - Convert time_t to UTC calendar time (reentrant)
 */
struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    if (!timep || !result) {
        return NULL;
    }
    
    time_t t = *timep;
    
    // Calculate seconds, minutes, hours
    result->tm_sec = t % 60;
    t /= 60;
    result->tm_min = t % 60;
    t /= 60;
    result->tm_hour = t % 24;
    t /= 24;
    
    // t is now days since epoch
    int days = (int)t;
    
    // Calculate day of week (Jan 1, 1970 was Thursday = 4)
    result->tm_wday = (days + 4) % 7;
    
    // Calculate year
    int year = 1970;
    while (1) {
        int year_days = is_leap_year(year) ? 366 : 365;
        if (days < year_days) {
            break;
        }
        days -= year_days;
        year++;
    }
    
    result->tm_year = year - 1900;
    result->tm_yday = days;
    
    // Calculate month and day
    int month = 0;
    while (month < 12) {
        int mdays = days_in_month(month, year);
        if (days < mdays) {
            break;
        }
        days -= mdays;
        month++;
    }
    
    result->tm_mon = month;
    result->tm_mday = days + 1;
    result->tm_isdst = 0;
    
    return result;
}

/*
 * gmtime() - Convert time_t to UTC calendar time (non-reentrant)
 */
struct tm *gmtime(const time_t *timep) {
    return gmtime_r(timep, &_tm_buffer);
}

/*
 * localtime_r() - Convert time_t to local calendar time (reentrant)
 */
struct tm *localtime_r(const time_t *timep, struct tm *result) {
    if (!timep || !result) {
        return NULL;
    }
    
    // Load profile if not already loaded
    if (!profile_loaded) {
        load_time_profile();
    }
    
    // Apply offset
    time_t local_time = *timep + profile_offset;
    
    return gmtime_r(&local_time, result);
}

/*
 * localtime() - Convert time_t to local calendar time (non-reentrant)
 */
struct tm *localtime(const time_t *timep) {
    return localtime_r(timep, &_tm_buffer);
}

/*
 * mktime() - Convert calendar time to time_t
 */
time_t mktime(struct tm *tm) {
    if (!tm) {
        return -1;
    }
    
    // Normalize values
    int year = tm->tm_year + 1900;
    int month = tm->tm_mon;
    int day = tm->tm_mday;
    
    // Calculate days since epoch
    int days = 0;
    
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    for (int m = 0; m < month; m++) {
        days += days_in_month(m, year);
    }
    
    days += day - 1;
    
    // Calculate seconds
    time_t seconds = (time_t)days * 86400;
    seconds += tm->tm_hour * 3600;
    seconds += tm->tm_min * 60;
    seconds += tm->tm_sec;
    
    // Update tm structure
    tm->tm_wday = day_of_week(year, month + 1, day);
    
    // Calculate day of year
    int yday = 0;
    for (int m = 0; m < month; m++) {
        yday += days_in_month(m, year);
    }
    yday += day - 1;
    tm->tm_yday = yday;
    
    return seconds;
}

/*
 * Helper: Parse 2-digit number
 */
static int parse_int(const char *str, int len) {
    int result = 0;
    for (int i = 0; i < len && str[i] >= '0' && str[i] <= '9'; i++) {
        result = result * 10 + (str[i] - '0');
    }
    return result;
}

/*
 * Helper: Append integer with padding
 */
static void append_int(char **out, int value, int width, char pad) {
    char temp[16];
    int i = 0;
    
    if (value == 0) {
        temp[i++] = '0';
    } else {
        int v = value;
        while (v > 0) {
            temp[i++] = '0' + (v % 10);
            v /= 10;
        }
    }
    
    // Pad if needed
    while (i < width) {
        temp[i++] = pad;
    }
    
    // Reverse and append
    for (int j = i - 1; j >= 0; j--) {
        *(*out)++ = temp[j];
    }
}

/*
 * format_time() - Format time according to GraceOS format string
 * 
 * Supported tokens:
 * DD - Day (01-31)
 * MM - Month (01-12)
 * YYYY - Year
 * HH - Hour (00-23)
 * hh - Hour (01-12)
 * mm - Minute
 * ss - Second
 * A - AM/PM
 */
int format_time(const struct tm *tm, const char *fmt, char *out, size_t max) {
    if (!tm || !fmt || !out || max == 0) {
        return -1;
    }
    
    char *p = out;
    const char *f = fmt;
    size_t remaining = max - 1;  // Reserve space for null terminator
    
    while (*f && remaining > 0) {
        if (f[0] == 'Y' && f[1] == 'Y' && f[2] == 'Y' && f[3] == 'Y') {
            // YYYY - Year
            int year = tm->tm_year + 1900;
            append_int(&p, year, 4, '0');
            f += 4;
            remaining -= 4;
        }
        else if (f[0] == 'M' && f[1] == 'M') {
            // MM - Month
            append_int(&p, tm->tm_mon + 1, 2, '0');
            f += 2;
            remaining -= 2;
        }
        else if (f[0] == 'D' && f[1] == 'D') {
            // DD - Day
            append_int(&p, tm->tm_mday, 2, '0');
            f += 2;
            remaining -= 2;
        }
        else if (f[0] == 'H' && f[1] == 'H') {
            // HH - 24-hour
            append_int(&p, tm->tm_hour, 2, '0');
            f += 2;
            remaining -= 2;
        }
        else if (f[0] == 'h' && f[1] == 'h') {
            // hh - 12-hour
            int hour = tm->tm_hour % 12;
            if (hour == 0) hour = 12;
            append_int(&p, hour, 2, '0');
            f += 2;
            remaining -= 2;
        }
        else if (f[0] == 'm' && f[1] == 'm') {
            // mm - Minute
            append_int(&p, tm->tm_min, 2, '0');
            f += 2;
            remaining -= 2;
        }
        else if (f[0] == 's' && f[1] == 's') {
            // ss - Second
            append_int(&p, tm->tm_sec, 2, '0');
            f += 2;
            remaining -= 2;
        }
        else if (f[0] == 'A') {
            // A - AM/PM
            const char *ampm = (tm->tm_hour < 12) ? "AM" : "PM";
            *p++ = ampm[0];
            *p++ = ampm[1];
            f++;
            remaining -= 2;
        }
        else {
            // Copy literal character
            *p++ = *f++;
            remaining--;
        }
    }
    
    *p = '\0';
    return p - out;
}

/*
 * strftime() - POSIX-compatible time formatting
 */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || !format || !tm || max == 0) {
        return 0;
    }
    
    char *p = s;
    const char *f = format;
    size_t remaining = max - 1;
    
    while (*f && remaining > 0) {
        if (*f == '%' && f[1]) {
            f++;
            switch (*f) {
                case 'Y':  // Year (4 digits)
                    append_int(&p, tm->tm_year + 1900, 4, '0');
                    remaining -= 4;
                    break;
                case 'm':  // Month (01-12)
                    append_int(&p, tm->tm_mon + 1, 2, '0');
                    remaining -= 2;
                    break;
                case 'd':  // Day (01-31)
                    append_int(&p, tm->tm_mday, 2, '0');
                    remaining -= 2;
                    break;
                case 'H':  // Hour 24h (00-23)
                    append_int(&p, tm->tm_hour, 2, '0');
                    remaining -= 2;
                    break;
                case 'I':  // Hour 12h (01-12)
                    {
                        int h = tm->tm_hour % 12;
                        if (h == 0) h = 12;
                        append_int(&p, h, 2, '0');
                        remaining -= 2;
                    }
                    break;
                case 'M':  // Minute
                    append_int(&p, tm->tm_min, 2, '0');
                    remaining -= 2;
                    break;
                case 'S':  // Second
                    append_int(&p, tm->tm_sec, 2, '0');
                    remaining -= 2;
                    break;
                case 'p':  // AM/PM
                    {
                        const char *ampm = (tm->tm_hour < 12) ? "AM" : "PM";
                        *p++ = ampm[0];
                        *p++ = ampm[1];
                        remaining -= 2;
                    }
                    break;
                case '%':  // Literal %
                    *p++ = '%';
                    remaining--;
                    break;
                default:
                    *p++ = '%';
                    *p++ = *f;
                    remaining -= 2;
                    break;
            }
            f++;
        } else {
            *p++ = *f++;
            remaining--;
        }
    }
    
    *p = '\0';
    return p - s;
}

/*
 * sleep() - Sleep for specified seconds
 */
void sleep(unsigned int seconds) {
    // TODO: Implement proper sleep using timer
    // For now, busy wait (placeholder)
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current);
        if (current.tv_sec - start.tv_sec >= seconds) {
            break;
        }
    }
}

/*
 * usleep() - Sleep for specified microseconds
 */
void usleep(unsigned int useconds) {
    // TODO: Implement proper sleep
    struct timespec start, current;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int64_t target_ns = (int64_t)useconds * 1000;
    
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current);
        int64_t elapsed_ns = (current.tv_sec - start.tv_sec) * 1000000000LL +
                            (current.tv_nsec - start.tv_nsec);
        if (elapsed_ns >= target_ns) {
            break;
        }
    }
}

// Profile management functions implemented in separate file
