// ============================
// GraceOS Power Management
// Syscall implementations
// ============================

#include "power.h"
#include "../../drivers/video/tty.h"

/* ============================
   Shutdown Flags
   ============================ */

#define SHUTDOWN_POWEROFF   0   /* Normal power off */
#define SHUTDOWN_HALT       1   /* Halt only (don't power off) */

/* ============================
   Reboot Flags
   ============================ */

#define REBOOT_COLD         0   /* Full reboot */
#define REBOOT_WARM         1   /* Warm reboot (skip POST) */

/* ============================
   SYS_SHUTDOWN Implementation
   ============================ */

long sys_shutdown(long flags, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    tty_set_color(TTY_YELLOW, TTY_BLACK);
    tty_print("\n[POWER] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    
    if (flags == SHUTDOWN_HALT)
    {
        tty_print("System halting...\n");
        tty_print("It is now safe to turn off your computer.\n");
        power_halt();
    }
    else
    {
        tty_print("System shutting down...\n");
        tty_print("Goodbye!\n\n");
        power_shutdown();
    }
    
    /* Should never return */
    return 0;
}

/* ============================
   SYS_REBOOT Implementation
   ============================ */

long sys_reboot(long flags, long unused1, long unused2, long unused3, long unused4, long unused5)
{
    (void)flags;
    (void)unused1; (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    
    tty_set_color(TTY_YELLOW, TTY_BLACK);
    tty_print("\n[POWER] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("System rebooting...\n\n");
    
    power_reboot();
    
    /* Should never return */
    return 0;
}
