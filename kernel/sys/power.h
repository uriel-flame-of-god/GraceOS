// ============================
// GraceOS Power Management
// ============================

#ifndef GRACEOS_POWER_H
#define GRACEOS_POWER_H

/* ============================
   Power Control Functions
   (Implemented in power.asm)
   ============================ */

/* Reboot the system via keyboard controller reset */
void power_reboot(void);

/* Shutdown the system (ACPI/QEMU) */
void power_shutdown(void);

/* Halt the CPU (low-power state) */
void power_halt(void);

/* ============================
   Syscall Wrappers
   ============================ */

/* SYS_SHUTDOWN - Shutdown system */
long sys_shutdown(long flags, long unused1, long unused2, long unused3, long unused4, long unused5);

/* SYS_REBOOT - Reboot system */
long sys_reboot(long flags, long unused1, long unused2, long unused3, long unused4, long unused5);

#endif /* GRACEOS_POWER_H */
