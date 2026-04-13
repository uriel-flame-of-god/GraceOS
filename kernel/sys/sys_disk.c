// ============================
// GraceOS Disk I/O Syscalls
// spm-enforced block device access
// ============================

#include "../include/syscall.h"
#include "../proc/proc.h"
#include "../spm/spm.h"
#include "../log/klog.h"
#include "../../drivers/storage/ide.h"

/* ============================
   Disk Info Structure
   (matches userland struct disk_info)
   ============================ */

typedef struct disk_info {
    uint64_t sector_count;
    uint64_t size_bytes;
    uint32_t sector_size;   /* Always 512 for IDE */
} disk_info_t;

/* ============================
   SYS_DISK_INFO
   arg: dev (0 = primary IDE), out_ptr -> disk_info_t
   ============================ */

long sys_disk_info(long dev, long out_ptr, long unused1, long unused2, long unused3, long unused4)
{
    (void)dev; (void)unused1; (void)unused2; (void)unused3; (void)unused4;

    /* Require READ permission on /dev/ */
    uint32_t uid = current ? (uint32_t)current->uid : 0;
    if (spm_check(uid, PERM_READ, "/dev/") < 0)
        return -1;

    if (!ide_ready())
        return -1;

    disk_info_t* info = (disk_info_t*)out_ptr;
    if (!info)
        return -1;

    info->sector_size  = 512;
    info->size_bytes   = ide_size();
    info->sector_count = ide_size() / 512;

    return 0;
}

/* ============================
   SYS_DISK_READ
   dev=0 (IDE primary master), lba, count (sectors), buf
   ============================ */

long sys_disk_read(long dev, long lba, long count, long buf, long unused1, long unused2)
{
    (void)dev; (void)unused1; (void)unused2;

    uint32_t uid = current ? (uint32_t)current->uid : 0;
    if (spm_check(uid, PERM_READ, "/dev/") < 0)
        return -1;

    if (!ide_ready())
        return -1;

    if (!buf || count <= 0)
        return -1;

    int result = ide_read((uint64_t)lba, (void*)buf, (uint32_t)count);
    return (long)result;
}

/* ============================
   SYS_DISK_WRITE
   dev=0 (IDE primary master), lba, count (sectors), buf
   ============================ */

long sys_disk_write(long dev, long lba, long count, long buf, long unused1, long unused2)
{
    (void)dev; (void)unused1; (void)unused2;

    uint32_t uid = current ? (uint32_t)current->uid : 0;
    if (spm_check(uid, PERM_WRITE, "/dev/") < 0)
    {
        klog_warn("sys_disk_write: permission denied");
        return -1;
    }

    if (!ide_ready())
        return -1;

    if (!buf || count <= 0)
        return -1;

    int result = ide_write((uint64_t)lba, (const void*)buf, (uint32_t)count);
    return (long)result;
}
