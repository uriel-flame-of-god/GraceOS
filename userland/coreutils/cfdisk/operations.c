// ============================
// GraceOS cfdisk — Partition Operations
// ============================

#include "operations.h"
#include "mbr.h"
#include "gpt.h"
#include "disk.h"
#include "../../../lib/libc/string.h"

static int ops_validate_gpt_layout(cfdisk_state_t* state)
{
    uint64_t first_usable = 34;
    uint64_t last_usable = state->disk.sector_count - 34;
    int count = 0;

    for (int i = 0; i < state->part_count; i++)
    {
        partition_t* pi = &state->parts[i];
        if (pi->is_deleted)
            continue;

        count++;
        if (count > MAX_PARTITIONS)
            return -1;
        if (pi->start_lba < first_usable || pi->end_lba > last_usable)
            return -1;
        if (pi->end_lba < pi->start_lba)
            return -1;

        for (int j = i + 1; j < state->part_count; j++)
        {
            partition_t* pj = &state->parts[j];
            if (pj->is_deleted)
                continue;

            if (pi->start_lba <= pj->end_lba && pj->start_lba <= pi->end_lba)
                return -1;
        }
    }

    return 0;
}

/* ============================
   Sort partitions by start_lba
   ============================ */

void ops_sort_partitions(cfdisk_state_t* state)
{
    /* Simple insertion sort — partition count is tiny */
    for (int i = 1; i < state->part_count; i++)
    {
        partition_t tmp = state->parts[i];
        int j = i - 1;
        while (j >= 0 && state->parts[j].start_lba > tmp.start_lba)
        {
            state->parts[j + 1] = state->parts[j];
            j--;
        }
        state->parts[j + 1] = tmp;
    }
}

/* ============================
   Rebuild free region list
   ============================ */

void ops_rebuild_free(cfdisk_state_t* state)
{
    ops_sort_partitions(state);

    state->free_count = 0;

    uint64_t usable_start = 34;          /* after GPT header / MBR + first track */
    uint64_t usable_end   = state->disk.sector_count - 34;

    uint64_t cursor = usable_start;

    for (int i = 0; i < state->part_count; i++)
    {
        partition_t* p = &state->parts[i];
        if (p->is_deleted)
            continue;

        if (p->start_lba > cursor)
        {
            if (state->free_count < CFDISK_MAX_FREE_REGIONS)
            {
                free_region_t* fr = &state->free[state->free_count++];
                fr->start_lba    = cursor;
                fr->size_sectors = p->start_lba - cursor;
            }
        }
        cursor = p->end_lba + 1;
    }

    if (cursor < usable_end)
    {
        if (state->free_count < CFDISK_MAX_FREE_REGIONS)
        {
            free_region_t* fr = &state->free[state->free_count++];
            fr->start_lba    = cursor;
            fr->size_sectors = usable_end - cursor;
        }
    }
}

/* ============================
   New partition
   ============================ */

int ops_new_partition(cfdisk_state_t* state, uint64_t size_sectors, uint8_t type)
{
    if (state->part_count >= MAX_PARTITIONS)
        return -1;

    ops_rebuild_free(state);

    /* Find the first free region large enough */
    free_region_t* chosen = NULL;
    for (int i = 0; i < state->free_count; i++)
    {
        if (state->free[i].size_sectors >= size_sectors)
        {
            chosen = &state->free[i];
            break;
        }
    }

    if (!chosen)
        return -1;

    uint64_t start = disk_align_lba(chosen->start_lba);
    uint64_t end   = start + size_sectors - 1;

    if (end > chosen->start_lba + chosen->size_sectors - 1)
        return -1;

    partition_t* p = &state->parts[state->part_count++];
    memset(p, 0, sizeof(*p));

    p->start_lba    = start;
    p->end_lba      = end;
    p->size_sectors = size_sectors;
    p->type         = type;
    p->is_new       = true;
    p->is_gpt       = (state->table_type == TABLE_GPT);

    p->name[0] = 'p'; p->name[1] = 'a'; p->name[2] = 'r';
    p->name[3] = 't'; p->name[4] = '0' + (char)state->part_count;
    p->name[5] = '\0';

    state->modified = true;
    ops_sort_partitions(state);
    return 0;
}

/* ============================
   Delete partition
   ============================ */

int ops_delete_partition(cfdisk_state_t* state)
{
    if (state->selected < 0 || state->selected >= state->part_count)
        return -1;

    state->parts[state->selected].is_deleted = true;
    state->modified = true;
    return 0;
}

/* ============================
   Commit changes
   ============================ */

int ops_commit(cfdisk_state_t* state)
{
    int result;

    if (state->table_type == TABLE_GPT)
    {
        result = ops_validate_gpt_layout(state);
        if (result < 0)
            return -1;
        result = gpt_write(state);
    }
    else
    {
        result = mbr_validate(state);
        if (result < 0)
            return -1;
        result = mbr_write(state);
    }

    if (result == 0)
        state->modified = false;

    return result;
}

int ops_make_efi_branchfs(cfdisk_state_t* state, uint64_t efi_size_mb)
{
    if (!state)
        return -1;

    if (efi_size_mb == 0)
        efi_size_mb = 512;

    uint64_t first_usable = 34;
    uint64_t last_usable = state->disk.sector_count - 34;
    if (last_usable <= first_usable)
        return -1;

    uint64_t efi_sectors = (efi_size_mb * 1024 * 1024) / SECTOR_SIZE;
    if (efi_sectors < ALIGNMENT_LBA)
        efi_sectors = ALIGNMENT_LBA;

    state->table_type = TABLE_GPT;
    state->part_count = 0;
    state->selected = 0;

    uint64_t efi_start = disk_align_lba(first_usable);
    uint64_t efi_end = efi_start + efi_sectors - 1;
    if (efi_end > last_usable)
        return -1;

    partition_t* efi = &state->parts[state->part_count++];
    memset(efi, 0, sizeof(*efi));
    efi->start_lba = efi_start;
    efi->end_lba = efi_end;
    efi->size_sectors = efi_sectors;
    efi->type = PART_TYPE_EFI;
    efi->is_new = true;
    efi->is_gpt = true;
    strncpy(efi->name, "EFI", sizeof(efi->name) - 1);
    efi->name[sizeof(efi->name) - 1] = '\0';

    uint64_t data_start = disk_align_lba(efi_end + 1);
    if (data_start > last_usable)
        return -1;

    partition_t* data = &state->parts[state->part_count++];
    memset(data, 0, sizeof(*data));
    data->start_lba = data_start;
    data->end_lba = last_usable;
    data->size_sectors = data->end_lba - data->start_lba + 1;
    data->type = PART_TYPE_GRACE;
    data->is_new = true;
    data->is_gpt = true;
    strncpy(data->name, "BranchFS", sizeof(data->name) - 1);
    data->name[sizeof(data->name) - 1] = '\0';

    state->modified = true;
    ops_sort_partitions(state);
    ops_rebuild_free(state);

    return 0;
}

/* ============================
   Selection movement
   ============================ */

void ops_select_prev(cfdisk_state_t* state)
{
    if (state->selected > 0)
        state->selected--;
}

void ops_select_next(cfdisk_state_t* state)
{
    if (state->selected < state->part_count - 1)
        state->selected++;
}
