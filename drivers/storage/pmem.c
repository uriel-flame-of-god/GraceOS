// ============================
// GraceOS Persistent Memory API
// IDE/NVMe-backed block access
// ============================

#include "pmem.h"
#include "ide.h"
#include "nvme.h"
#include "../../kernel/log/klog.h"

typedef enum {
    PMEM_BACKEND_NONE = 0,
    PMEM_BACKEND_NVME,
    PMEM_BACKEND_IDE
} pmem_backend_t;

static pmem_backend_t pmem_backend = PMEM_BACKEND_NONE;
static uint64_t pmem_bytes = 0;

int pmem_init(void)
{
    pmem_backend = PMEM_BACKEND_NONE;
    pmem_bytes = 0;

    if (nvme_init())
    {
        pmem_backend = PMEM_BACKEND_NVME;
        pmem_bytes = nvme_size();
    }
    else if (ide_init())
    {
        pmem_backend = PMEM_BACKEND_IDE;
        pmem_bytes = ide_size();
    }

    if (pmem_backend == PMEM_BACKEND_NONE || pmem_bytes == 0)
    {
        klog_warn("PMEM: No persistent memory backend found");
        pmem_backend = PMEM_BACKEND_NONE;
        pmem_bytes = 0;
        return 0;
    }

    klog_log("PMEM: Backend ready");
    return 1;
}

int pmem_ready(void)
{
    return pmem_backend != PMEM_BACKEND_NONE;
}

uint64_t pmem_size(void)
{
    return pmem_bytes;
}

int pmem_read(uint64_t offset, void* buffer, uint64_t size)
{
    if (!pmem_ready() || !buffer || size == 0)
        return -1;

    if ((offset % PMEM_SECTOR_SIZE) != 0 || (size % PMEM_SECTOR_SIZE) != 0)
        return -1;

    if (offset + size > pmem_bytes)
        return -1;

    uint64_t lba = offset / PMEM_SECTOR_SIZE;
    uint32_t sectors = (uint32_t)(size / PMEM_SECTOR_SIZE);

    if (pmem_backend == PMEM_BACKEND_NVME)
        return nvme_read(lba, buffer, sectors);
    if (pmem_backend == PMEM_BACKEND_IDE)
        return ide_read(lba, buffer, sectors);

    return -1;
}

int pmem_write(uint64_t offset, const void* buffer, uint64_t size)
{
    if (!pmem_ready() || !buffer || size == 0)
        return -1;

    if ((offset % PMEM_SECTOR_SIZE) != 0 || (size % PMEM_SECTOR_SIZE) != 0)
        return -1;

    if (offset + size > pmem_bytes)
        return -1;

    uint64_t lba = offset / PMEM_SECTOR_SIZE;
    uint32_t sectors = (uint32_t)(size / PMEM_SECTOR_SIZE);

    if (pmem_backend == PMEM_BACKEND_NVME)
        return nvme_write(lba, buffer, sectors);
    if (pmem_backend == PMEM_BACKEND_IDE)
        return ide_write(lba, buffer, sectors);

    return -1;
}

const char* pmem_backend_name(void)
{
    if (pmem_backend == PMEM_BACKEND_NVME)
        return "NVMe";
    if (pmem_backend == PMEM_BACKEND_IDE)
        return "IDE";
    return "None";
}
