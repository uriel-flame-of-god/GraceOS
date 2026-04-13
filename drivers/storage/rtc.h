/*
 * RTC (Real-Time Clock) Driver Header
 * Provides interface for reading hardware clock at boot
 */

#ifndef DRIVERS_RTC_H
#define DRIVERS_RTC_H

#include "../../lib/libc/int.h"

// CMOS RTC I/O ports
#define RTC_PORT_INDEX  0x70
#define RTC_PORT_DATA   0x71

// CMOS RTC registers
#define RTC_REG_SECONDS     0x00
#define RTC_REG_MINUTES     0x02
#define RTC_REG_HOURS       0x04
#define RTC_REG_DAY         0x07
#define RTC_REG_MONTH       0x08
#define RTC_REG_YEAR        0x09
#define RTC_REG_CENTURY     0x32
#define RTC_REG_STATUS_A    0x0A
#define RTC_REG_STATUS_B    0x0B

// RTC Status Register B flags
#define RTC_STATUS_B_24HOUR 0x02
#define RTC_STATUS_B_BINARY 0x04

/*
 * RTC time structure
 */
struct rtc_time {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

/*
 * RTC driver functions
 */
void rtc_init(void);
int rtc_read_time(struct rtc_time *time);
void rtc_wait_update(void);
uint8_t rtc_read_register(uint8_t reg);
void rtc_write_register(uint8_t reg, uint8_t value);

/*
 * Convert BCD to binary (for RTC data)
 */
static inline uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/*
 * Convert binary to BCD
 */
static inline uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

#endif // DRIVERS_RTC_H
