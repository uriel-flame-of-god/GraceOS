#ifdef ARCH_ARM64

#include "power.h"
#include "io/port.h"

#ifndef RPI_BASE
#define RPI_BASE 0x3F000000UL
#endif

#define PM_BASE (RPI_BASE + 0x100000UL)
#define PM_RSTC (PM_BASE + 0x1C)
#define PM_RSTS (PM_BASE + 0x20)
#define PM_WDOG (PM_BASE + 0x24)

#define PM_PASSWORD 0x5A000000
#define PM_RSTC_WRCFG_FULL_RESET 0x20

void rpi_reboot(void)
{
    mmio_write32(PM_RSTS, PM_PASSWORD);
    mmio_write32(PM_WDOG, PM_PASSWORD | 1);
    mmio_write32(PM_RSTC, PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET);
    for (;;)
        __asm__ volatile ("wfe");
}

void rpi_shutdown(void)
{
    for (;;)
        __asm__ volatile ("wfe");
}

#endif /* ARCH_ARM64 */
