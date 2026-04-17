// ============================
// GraceOS ARM64 Kernel Entry
// Raspberry Pi 3 / 4
// ============================
//
// Stripped-down boot sequence for the initial ARM64 port.
// Scope: UART + framebuffer output, basic heap, proc/sched, sysinfo.
//
// NOT included in this port (x86-only features):
//   Multiboot / PMM from memory map, VMM / MMU, IDT, VBE,
//   network stack, audio, most drivers.

#ifdef ARCH_ARM64

#include "../../drivers/video/tty.h"
#include "../../drivers/video/uart_rpi.h"
#include "../arch/arm64/power.h"
#include "../log/klog.h"
#include "../mm/kheap.h"
#include "../spm/spm.h"
#include "sysinfo.h"
#include "../../lib/libc/string.h"

/* ============================
   Minimal console loop (UART input, framebuffer+UART output)
   No history, no escape sequences — plain safe readline.
   ============================ */

#define CONSOLE_MAX_LINE 128

static size_t console_readline(char* buf, size_t max_len)
{
    size_t pos = 0;

    while (1)
    {
        char c = uart_getchar();        /* blocking — classify BEFORE echoing */

        /* Enter */
        if (c == '\r' || c == '\n')
        {
            tty_putchar('\n');          /* \r\n on UART, \n on FB */
            buf[pos] = '\0';
            return pos;
        }

        /* Ctrl+C */
        if (c == 0x03)
        {
            tty_print("^C\n");
            buf[0] = '\0';
            return 0;
        }

        /* ESC — consume the full ANSI sequence without echoing.
           Arrow keys are ESC [ <final>.  All bytes of a sequence
           arrive together in the UART RX FIFO (115200 baud delivers
           3 bytes in ~26 µs, far faster than software can respond),
           so uart_rx_ready() is reliable here — no NOP busy-wait.
           ANSI CSI final bytes are in the range 0x40–0x7E ('@'–'~'). */
        if (c == 0x1B)
        {
            while (uart_rx_ready())
            {
                char ec = uart_getchar();
                if (ec >= 0x40 && ec <= 0x7E)
                    break;              /* consumed sequence terminator */
            }
            continue;
        }

        /* Backspace / DEL */
        if (c == '\b' || c == 127)
        {
            if (pos > 0)
            {
                pos--;
                tty_print("\b \b");    /* erase on UART terminal and FB */
            }
            continue;
        }

        /* Printable ASCII — echo then store */
        if (c >= 32 && c < 127)
        {
            if (pos < max_len - 1)
            {
                tty_putchar(c);        /* echo to UART + FB */
                buf[pos++] = c;
            }
            continue;
        }
    }
}

static void console_help(void)
{
    tty_print("Commands: help, sysinfo, echo <text>, clear, shutdown, reboot\n");
}

static void console_handle_command(const char* line)
{
    if (strcmp(line, "help") == 0)
    {
        console_help();
        return;
    }

    if (strcmp(line, "clear") == 0)
    {
        tty_clear();
        return;
    }

    if (strcmp(line, "sysinfo") == 0)
    {
        sysinfo_display();
        return;
    }

    if (strcmp(line, "shutdown") == 0)
    {
        tty_print("Shutting down...\n");
        rpi_shutdown();
        return;
    }

    if (strcmp(line, "reboot") == 0)
    {
        tty_print("Rebooting...\n");
        rpi_reboot();
        return;
    }

    if (strncmp(line, "echo ", 5) == 0)
    {
        tty_print(line + 5);
        tty_print("\n");
        return;
    }

    if (strcmp(line, "echo") == 0)
    {
        tty_print("\n");
        return;
    }

    tty_print("Unknown command: ");
    tty_print(line);
    tty_print("\n");
}

static void console_loop(void)
{
    char line[128];

    tty_print("Minimal console ready. Type 'help' for commands.\n");

    while (1)
    {
        tty_print("grace> ");
        console_readline(line, sizeof(line));

        if (line[0] == '\0')
            continue;

        console_handle_command(line);
    }
}

/* ============================
   kmain — ARM64 boot entry from boot/arm64/boot.S
   x0 = 0 (no multiboot on RPi)
   ============================ */

void kmain(uint64_t unused)
{
    (void)unused;

    /* --- Output subsystem ----------------------------------------- */
    tty_init();          /* Initialises UART via uart_init() */
    klog_init();

    /* --- Banner ---------------------------------------------------- */
    tty_set_color(TTY_WHITE, TTY_BLUE);
    tty_print("                                                                                ");
    tty_print("                      Lumina Kernel v0.3.5  [ARM64/RPi]                        ");
    tty_print("                                                                                ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("\n\n");

    klog_init_msg("ARM64 boot sequence started");

    /* --- Memory ---------------------------------------------------- */
    klog_init_msg("Initializing kernel heap");
    kheap_init();
    klog_logn("Heap ready");

    /* --- System info ----------------------------------------------- */
    sysinfo_init();

    klog_logn("Boot complete");
    tty_print("\n");

    console_loop();
}

#endif /* ARCH_ARM64 */
