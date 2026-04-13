// ============================
// GraceOS cfdisk — MBR Read/Write
// ============================

#include "mbr.h"
#include "disk.h"
#include "../../../lib/libc/string.h"

/* ============================
   MBR Read
   ============================ */

int mbr_read(cfdisk_state_t* state)
{
    uint8_t sector[SECTOR_SIZE];

    if (disk_read(state->dev, 0, 1, sector) != 0)
        return -1;

    mbr_t* mbr = (mbr_t*)sector;

    if (mbr->signature != MBR_SIGNATURE)
        return -1;

    /* Check for GPT protective entry */
    for (int i = 0; i < 4; i++)
    {
        if (mbr->entries[i].type == PART_TYPE_GPT)
        {
            state->table_type = TABLE_GPT;
            return 1;  /* Caller should use gpt_read instead */
        }
    }

    state->table_type = TABLE_MBR;
    state->part_count = 0;

    for (int i = 0; i < 4; i++)
    {
        mbr_entry_t* e = &mbr->entries[i];
        if (e->type == PART_TYPE_EMPTY || e->lba_size == 0)
            continue;

        partition_t* p = &state->parts[state->part_count++];
        memset(p, 0, sizeof(*p));

        p->start_lba    = (uint64_t)e->lba_start;
        p->size_sectors = (uint64_t)e->lba_size;
        p->end_lba      = p->start_lba + p->size_sectors - 1;
        p->type         = e->type;
        p->is_gpt       = false;

        /* Build a default name from type */
        p->name[0] = 'p';
        p->name[1] = 'a';
        p->name[2] = 'r';
        p->name[3] = 't';
        p->name[4] = '0' + (char)(state->part_count);
        p->name[5] = '\0';
    }

    return 0;
}

/* ============================
   MBR Write
   ============================ */

int mbr_write(cfdisk_state_t* state)
{
    uint8_t sector[SECTOR_SIZE];

    /* Read existing MBR to preserve boot code */
    if (disk_read(state->dev, 0, 1, sector) != 0)
        return -1;

    mbr_t* mbr = (mbr_t*)sector;

    /* Zero out the 4 partition entries only — boot code preserved */
    memset(mbr->entries, 0, sizeof(mbr->entries));

    int entry_idx = 0;
    for (int i = 0; i < state->part_count && entry_idx < 4; i++)
    {
        partition_t* p = &state->parts[i];

        if (p->is_deleted || p->is_gpt)
            continue;

        mbr_entry_t* e = &mbr->entries[entry_idx++];
        e->status    = 0x00;
        e->type      = p->type;
        e->lba_start = (uint32_t)p->start_lba;
        e->lba_size  = (uint32_t)p->size_sectors;
        /* CHS fields left as zero — LBA mode used */
    }

    mbr->signature = MBR_SIGNATURE;

    return disk_write(state->dev, 0, 1, sector);
}

/* ============================
   Validation
   ============================ */

int mbr_validate(cfdisk_state_t* state)
{
    int count = 0;

    for (int i = 0; i < state->part_count; i++)
    {
        partition_t* pi = &state->parts[i];
        if (pi->is_deleted)
            continue;

        count++;
        if (count > MAX_MBR_PARTS)
            return -1;

        /* Must be within disk bounds */
        if (pi->end_lba >= state->disk.sector_count)
            return -1;

        /* Check for overlap with every subsequent partition */
        for (int j = i + 1; j < state->part_count; j++)
        {
            partition_t* pj = &state->parts[j];
            if (pj->is_deleted)
                continue;

            if (pi->start_lba <= pj->end_lba && pj->start_lba <= pi->end_lba)
                return -1;  /* Overlap */
        }
    }

    return 0;
}
