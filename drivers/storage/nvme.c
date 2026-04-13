// ============================
// GraceOS NVMe Driver (Stub)
// ============================

#include "nvme.h"

int nvme_init(void)
{
    return 0;
}

int nvme_ready(void)
{
    return 0;
}

uint64_t nvme_size(void)
{
    return 0;
}

int nvme_read(uint64_t lba, void* buffer, uint32_t sectors)
{
    (void)lba;
    (void)buffer;
    (void)sectors;
    return -1;
}

int nvme_write(uint64_t lba, const void* buffer, uint32_t sectors)
{
    (void)lba;
    (void)buffer;
    (void)sectors;
    return -1;
}
