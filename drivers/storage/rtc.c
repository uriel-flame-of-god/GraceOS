/*
 * RTC (Real-Time Clock) Driver Implementation
 * Reads CMOS RTC to initialize system time at boot
 */

#include "rtc.h"
#include "../../kernel/include/time.h"

// Forward declaration for I/O functions
extern void outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);

/*
 * Read a register from the RTC
 */
uint8_t rtc_read_register(uint8_t reg) {
    outb(RTC_PORT_INDEX, reg);
    return inb(RTC_PORT_DATA);
}

/*
 * Write a register to the RTC
 */
void rtc_write_register(uint8_t reg, uint8_t value) {
    outb(RTC_PORT_INDEX, reg);
    outb(RTC_PORT_DATA, value);
}

/*
 * Wait for RTC update to complete
 */
void rtc_wait_update(void) {
    // Wait until update-in-progress flag is clear
    while (rtc_read_register(RTC_REG_STATUS_A) & 0x80) {
        // Busy wait
    }
}

/*
 * Read current time from RTC
 */
int rtc_read_time(struct rtc_time *time) {
    if (!time) {
        return -1;
    }
    
    uint8_t century = 0;
    uint8_t status_b;
    
    // Wait for update to complete
    rtc_wait_update();
    
    // Read time values
    time->second = rtc_read_register(RTC_REG_SECONDS);
    time->minute = rtc_read_register(RTC_REG_MINUTES);
    time->hour = rtc_read_register(RTC_REG_HOURS);
    time->day = rtc_read_register(RTC_REG_DAY);
    time->month = rtc_read_register(RTC_REG_MONTH);
    time->year = rtc_read_register(RTC_REG_YEAR);
    
    // Try to read century (not all RTCs support this)
    century = rtc_read_register(RTC_REG_CENTURY);
    
    // Check data format
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    
    // Convert from BCD to binary if needed
    if (!(status_b & RTC_STATUS_B_BINARY)) {
        time->second = bcd_to_bin(time->second);
        time->minute = bcd_to_bin(time->minute);
        time->hour = bcd_to_bin(time->hour);
        time->day = bcd_to_bin(time->day);
        time->month = bcd_to_bin(time->month);
        time->year = bcd_to_bin(time->year);
        
        if (century) {
            century = bcd_to_bin(century);
        }
    }
    
    // Calculate full year
    if (century) {
        time->year = century * 100 + time->year;
    } else {
        // Assume 21st century for years < 70, 20th century otherwise
        time->year += (time->year < 70) ? 2000 : 1900;
    }
    
    return 0;
}

/*
 * Initialize RTC and set system time
 * Called during kernel boot
 */
void rtc_init(void) {
    struct rtc_time rtc;
    
    // Read time from RTC
    if (rtc_read_time(&rtc) < 0) {
        // Failed to read RTC, set time to epoch
        system_time.tv_sec = 0;
        system_time.tv_nsec = 0;
        return;
    }
    
    // Initialize system time from RTC
    rtc_init_time(rtc.year, rtc.month, rtc.day,
                  rtc.hour, rtc.minute, rtc.second);
}
