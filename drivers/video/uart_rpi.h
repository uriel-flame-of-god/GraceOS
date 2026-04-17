#ifndef GRACEOS_UART_RPI_H
#define GRACEOS_UART_RPI_H

#ifdef ARCH_ARM64

// ============================
// GraceOS RPi UART Driver
// ============================
//
// Supports two UART variants:
//
//   mini-UART (AUX UART)
//     Enabled by:  enable_uart=1  in config.txt  (default)
//     GPIO pins:   14 (TX), 15 (RX) via ALT5
//     Baud calc:   AUX_MU_BAUD = (sys_clock / (8 * baud)) - 1
//
//   PL011 UART
//     Enabled by:  dtoverlay=disable-bt  in config.txt
//     GPIO pins:   14 (TX), 15 (RX) via ALT0
//     Baud calc:   integer + fractional divisor from 48 MHz clock
//
// Default: mini-UART (matches the provided config.txt enable_uart=1).
// Define USE_PL011 before including this header to switch to PL011.
//
// Peripheral base addresses:
//   RPI_BASE  0x3F000000   Raspberry Pi 3 (BCM2837)
//             0xFE000000   Raspberry Pi 4 (BCM2711)
// Set via -DRPI_BASE=... in CFLAGS.

/* Unified init / putchar / getchar / print */
void uart_init(void);
void uart_putchar(char c);          /* auto-inserts \r before \n */
char uart_getchar(void);            /* blocking */
int  uart_rx_ready(void);           /* non-blocking check: 1 = char available */
char uart_getchar_nb(void);         /* non-blocking: returns 0 if empty */
void uart_print(const char* s);

/* Low-level variants (if you need to select one explicitly) */
void uart_mini_init(void);
void uart_mini_putchar(char c);
char uart_mini_getchar(void);
int  uart_mini_rx_ready(void);

void uart_pl011_init(void);
void uart_pl011_putchar(char c);
char uart_pl011_getchar(void);
int  uart_pl011_rx_ready(void);

#endif /* ARCH_ARM64 */
#endif /* GRACEOS_UART_RPI_H */
