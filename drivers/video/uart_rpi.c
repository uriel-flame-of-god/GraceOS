// ============================
// GraceOS RPi UART Driver
// Raspberry Pi 3 / 4 (AArch64)
// ============================
//
// See uart_rpi.h for usage notes and config.txt requirements.

#ifdef ARCH_ARM64

#include "uart_rpi.h"
#include "../../lib/libc/int.h"

// ----------------------------------------------------------------
// Peripheral base address
// RPi 3 (BCM2837): 0x3F000000
// RPi 4 (BCM2711): 0xFE000000
// Override with -DRPI_BASE=0xFE000000 for Pi 4
// ----------------------------------------------------------------
#ifndef RPI_BASE
#define RPI_BASE 0x3F000000UL
#endif

// ----------------------------------------------------------------
// GPIO registers
// ----------------------------------------------------------------
#define GPIO_BASE       (RPI_BASE + 0x200000UL)
#define GPFSEL1         (*(volatile uint32_t*)(GPIO_BASE + 0x04))
#define GPPUD           (*(volatile uint32_t*)(GPIO_BASE + 0x94))
#define GPPUDCLK0       (*(volatile uint32_t*)(GPIO_BASE + 0x98))

// ----------------------------------------------------------------
// mini-UART (AUX UART) registers
// Selected by enable_uart=1 in config.txt (the default)
// ----------------------------------------------------------------
#define AUX_BASE        (RPI_BASE + 0x215000UL)
#define AUX_ENABLES     (*(volatile uint32_t*)(AUX_BASE + 0x04))
#define AUX_MU_IO       (*(volatile uint32_t*)(AUX_BASE + 0x40))
#define AUX_MU_IER      (*(volatile uint32_t*)(AUX_BASE + 0x44))
#define AUX_MU_IIR      (*(volatile uint32_t*)(AUX_BASE + 0x48))
#define AUX_MU_LCR      (*(volatile uint32_t*)(AUX_BASE + 0x4C))
#define AUX_MU_MCR      (*(volatile uint32_t*)(AUX_BASE + 0x50))
#define AUX_MU_LSR      (*(volatile uint32_t*)(AUX_BASE + 0x54))
#define AUX_MU_CNTL     (*(volatile uint32_t*)(AUX_BASE + 0x60))
#define AUX_MU_BAUD     (*(volatile uint32_t*)(AUX_BASE + 0x68))

// AUX_MU_LSR bits
#define AUX_MU_LSR_TX_EMPTY  (1 << 5)   // TX FIFO has space
#define AUX_MU_LSR_RX_READY  (1 << 0)   // RX data available

// ----------------------------------------------------------------
// PL011 UART registers
// Needs dtoverlay=disable-bt in config.txt to free it from Bluetooth
// ----------------------------------------------------------------
#define PL011_BASE      (RPI_BASE + 0x201000UL)
#define PL011_DR        (*(volatile uint32_t*)(PL011_BASE + 0x00))
#define PL011_FR        (*(volatile uint32_t*)(PL011_BASE + 0x18))
#define PL011_IBRD      (*(volatile uint32_t*)(PL011_BASE + 0x24))
#define PL011_FBRD      (*(volatile uint32_t*)(PL011_BASE + 0x28))
#define PL011_LCRH      (*(volatile uint32_t*)(PL011_BASE + 0x2C))
#define PL011_CR        (*(volatile uint32_t*)(PL011_BASE + 0x30))
#define PL011_IMSC      (*(volatile uint32_t*)(PL011_BASE + 0x38))
#define PL011_ICR       (*(volatile uint32_t*)(PL011_BASE + 0x44))

#define PL011_FR_TXFF   (1 << 5)    // TX FIFO full
#define PL011_FR_RXFE   (1 << 4)    // RX FIFO empty

// ----------------------------------------------------------------
// Busy-wait delay (used only during UART init before we have a timer)
// ----------------------------------------------------------------
static void uart_delay(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        __asm__ volatile("nop");
}

// ================================================================
// mini-UART (AUX UART)
// Enabled by config.txt: enable_uart=1
// ================================================================

void uart_mini_init(void)
{
    // Enable mini-UART peripheral
    AUX_ENABLES = 1;
    AUX_MU_IER  = 0;        // No interrupts
    AUX_MU_CNTL = 0;        // Disable TX/RX while configuring
    AUX_MU_LCR  = 3;        // 8-bit mode
    AUX_MU_MCR  = 0;        // RTS high (flow control off)
    AUX_MU_IER  = 0;        // Interrupts still off
    AUX_MU_IIR  = 0xC6;     // Clear FIFO

    // Baud rate: (sys_clock / (8 * baud)) - 1
    // Default config.txt core_freq = 250 MHz, baud = 115200
    // → (250000000 / (8 * 115200)) - 1 = 270
    AUX_MU_BAUD = 270;

    // GPIO 14 and 15 → ALT5 (mini-UART TX/RX)
    // GPFSEL1 covers GPIO 10-19: pin 14 at bits [14:12], pin 15 at [17:15]
    // ALT5 = 0b010
    uint32_t ra = GPFSEL1;
    ra &= ~((7u << 12) | (7u << 15));  // Clear function for pins 14, 15
    ra |=  ((2u << 12) | (2u << 15));  // Set ALT5
    GPFSEL1 = ra;

    // Disable pull-up/down on GPIO 14, 15 (Pi 3 / BCM2837 method)
    GPPUD = 0;
    uart_delay(150);
    GPPUDCLK0 = (1u << 14) | (1u << 15);
    uart_delay(150);
    GPPUDCLK0 = 0;

    // Enable TX and RX
    AUX_MU_CNTL = 3;
}

void uart_mini_putchar(char c)
{
    while (!(AUX_MU_LSR & AUX_MU_LSR_TX_EMPTY))
        ;
    AUX_MU_IO = (uint32_t)(uint8_t)c;
}

char uart_mini_getchar(void)
{
    while (!(AUX_MU_LSR & AUX_MU_LSR_RX_READY))
        ;
    return (char)(AUX_MU_IO & 0xFF);
}

int uart_mini_rx_ready(void)
{
    return (AUX_MU_LSR & AUX_MU_LSR_RX_READY) ? 1 : 0;
}

// ================================================================
// PL011 UART
// Requires config.txt: dtoverlay=disable-bt
// ================================================================

void uart_pl011_init(void)
{
    // Disable UART before reconfiguring
    PL011_CR = 0;

    // GPIO 14, 15 → ALT0 (PL011 TX/RX)
    // ALT0 = 0b100
    uint32_t ra = GPFSEL1;
    ra &= ~((7u << 12) | (7u << 15));
    ra |=  ((4u << 12) | (4u << 15));
    GPFSEL1 = ra;

    // Disable pull-up/down (Pi 3 method)
    GPPUD = 0;
    uart_delay(150);
    GPPUDCLK0 = (1u << 14) | (1u << 15);
    uart_delay(150);
    GPPUDCLK0 = 0;

    // Clear pending interrupts
    PL011_ICR = 0x7FF;

    // Baud rate from 48 MHz UART clock (config.txt: init_uart_clock=48000000)
    // UART_CLK / (16 * baud) = 48000000 / (16 * 115200) ≈ 26.042
    // IBRD = 26, FBRD = round(0.042 * 64) = 3
    PL011_IBRD = 26;
    PL011_FBRD = 3;

    // 8-bit, no parity, 1 stop bit, FIFO enable
    // LCRH: WLEN=0b11 (8-bit) = bits[6:5], FEN = bit4
    PL011_LCRH = (3u << 5) | (1u << 4);

    // Mask all interrupts (polling mode)
    PL011_IMSC = 0;

    // Enable UART, TX, RX: UARTEN(0) | TXE(8) | RXE(9)
    PL011_CR = (1u << 0) | (1u << 8) | (1u << 9);
}

void uart_pl011_putchar(char c)
{
    while (PL011_FR & PL011_FR_TXFF)
        ;
    PL011_DR = (uint32_t)(uint8_t)c;
}

char uart_pl011_getchar(void)
{
    while (PL011_FR & PL011_FR_RXFE)
        ;
    return (char)(PL011_DR & 0xFF);
}

int uart_pl011_rx_ready(void)
{
    return (PL011_FR & PL011_FR_RXFE) ? 0 : 1;
}

// ================================================================
// Unified API
// Default: mini-UART (matches config.txt enable_uart=1)
// Define USE_PL011 to switch to PL011 (needs dtoverlay=disable-bt)
// ================================================================

void uart_init(void)
{
#ifdef USE_PL011
    uart_pl011_init();
#else
    uart_mini_init();
#endif
}

void uart_putchar(char c)
{
    // Always send \r before \n so terminals scroll correctly
    if (c == '\n')
        uart_putchar('\r');
#ifdef USE_PL011
    uart_pl011_putchar(c);
#else
    uart_mini_putchar(c);
#endif
}

char uart_getchar(void)
{
#ifdef USE_PL011
    return uart_pl011_getchar();
#else
    return uart_mini_getchar();
#endif
}

int uart_rx_ready(void)
{
#ifdef USE_PL011
    return uart_pl011_rx_ready();
#else
    return uart_mini_rx_ready();
#endif
}

char uart_getchar_nb(void)
{
    if (!uart_rx_ready())
        return 0;
    return uart_getchar();
}

void uart_print(const char* s)
{
    while (*s)
        uart_putchar(*s++);
}

#endif /* ARCH_ARM64 */
