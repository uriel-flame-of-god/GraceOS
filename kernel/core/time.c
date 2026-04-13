/*
 * GraceOS Time Management
 * Implements kernel-level timekeeping using UTC epoch
 */

#include "../include/time.h"
#include "../../lib/libc/string.h"

// Global time variables
struct timespec system_time = { 0, 0 };      // System realtime clock (UTC)
struct timespec monotonic_time = { 0, 0 };   // Monotonic clock (never adjusted)
static uint64_t boot_ticks = 0;              // Ticks since boot

// Days in each month (non-leap year)
static const int month_days[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * Check if a year is a leap year
 */
int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*
 * Get number of days in a month
 */
int days_in_month(int month, int year) {
    if (month < 1 || month > 12) {
        return 0;
    }
    
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    
    return month_days[month - 1];
}

/*
 * Calculate days since Unix epoch (1970-01-01)
 */
int days_since_epoch(int year, int month, int day) {
    int days = 0;
    
    // Add days for complete years
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    // Add days for complete months in current year
    for (int m = 1; m < month; m++) {
        days += days_in_month(m, year);
    }
    
    // Add remaining days
    days += day - 1;
    
    return days;
}

/*
 * Initialize time subsystem
 * Called during kernel boot after RTC has been read
 */
void time_init(void) {
    // RTC driver should have already set system_time
    // This function can perform additional initialization if needed
    
    // Initialize monotonic clock to match system time
    monotonic_time.tv_sec = system_time.tv_sec;
    monotonic_time.tv_nsec = system_time.tv_nsec;
}

/*
 * Timer tick handler
 * Called on each timer interrupt (1000 Hz = every 1 ms)
 */
void timer_tick(void) {
    // Increment boot ticks
    boot_ticks++;
    
    // Update system time
    system_time.tv_nsec += TICK_NS;
    
    if (system_time.tv_nsec >= 1000000000) {
        system_time.tv_sec++;
        system_time.tv_nsec -= 1000000000;
    }
    
    // Update monotonic time
    monotonic_time.tv_nsec += TICK_NS;
    
    if (monotonic_time.tv_nsec >= 1000000000) {
        monotonic_time.tv_sec++;
        monotonic_time.tv_nsec -= 1000000000;
    }
    
    // TODO: Wake sleeping threads
    // TODO: Update timers
}

/*
 * Get milliseconds since boot
 */
uint64_t timer_get_ms(void) {
    return boot_ticks;
}

/*
 * Get current time in seconds (UTC)
 */
time_t get_time_seconds(void) {
    return system_time.tv_sec;
}

/*
 * Get current time with nanosecond precision
 */
void get_time(struct timespec *ts) {
    if (!ts) {
        return;
    }
    
    ts->tv_sec = system_time.tv_sec;
    ts->tv_nsec = system_time.tv_nsec;
}

/*
 * Get monotonic time
 */
void get_monotonic(struct timespec *ts) {
    if (!ts) {
        return;
    }
    
    ts->tv_sec = monotonic_time.tv_sec;
    ts->tv_nsec = monotonic_time.tv_nsec;
}

/*
 * Set system time (requires root privileges - check done in syscall layer)
 * Returns 0 on success, -1 on failure
 */
int set_time(const struct timespec *ts) {
    if (!ts) {
        return -1;
    }
    
    // Validate input
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000) {
        return -1;
    }
    
    // Update system time (but not monotonic time)
    system_time.tv_sec = ts->tv_sec;
    system_time.tv_nsec = ts->tv_nsec;
    
    // TODO: Log time change for security
    
    return 0;
}

/*
 * Convert BCD to binary (for RTC)
 */
static inline uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/*
 * Initialize system time from RTC
 * This should be called during boot by the RTC driver
 */
void rtc_init_time(int year, int month, int day, int hour, int minute, int second) {
    // Calculate seconds since Unix epoch
    int64_t days = days_since_epoch(year, month, day);
    int64_t seconds = days * 86400LL;  // 86400 seconds per day
    
    seconds += hour * 3600;
    seconds += minute * 60;
    seconds += second;
    
    // Set system time
    system_time.tv_sec = seconds;
    system_time.tv_nsec = 0;
}
