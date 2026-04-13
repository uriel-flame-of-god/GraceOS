// ============================
// GraceOS cfdisk — GPT Read/Write/Validate
// ============================

#include "gpt.h"
#include "disk.h"
#include "mbr.h"
#include "../../../lib/libc/string.h"

/* ============================
   CRC32 (standard polynomial 0xEDB88320)
   ============================ */

uint32_t crc32_compute(const void* data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;

    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFF;
}

static const uint8_t gpt_guid_efi_system[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static const uint8_t gpt_guid_basic_data[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static const uint8_t gpt_guid_grace_branchfs[16] = {
    0x5B, 0x1E, 0x9F, 0x4D, 0x3A, 0x1E, 0x7D, 0x4C,
    0x9C, 0x21, 0x7C, 0x8C, 0x3D, 0x2A, 0x9B, 0x1E
};

static void gpt_set_guid(uint8_t* dst, const uint8_t src[16])
{
    memcpy(dst, src, 16);
}

static int gpt_guid_is_empty(const uint8_t* guid)
{
    for (int i = 0; i < 16; i++)
        if (guid[i] != 0)
            return 0;
    return 1;
}

static int gpt_guid_match(const uint8_t* a, const uint8_t* b)
{
    for (int i = 0; i < 16; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static const uint8_t* gpt_guid_for_type(uint8_t type)
{
    if (type == PART_TYPE_EFI)
        return gpt_guid_efi_system;
    if (type == PART_TYPE_GRACE)
        return gpt_guid_grace_branchfs;
    return gpt_guid_basic_data;
}

/* ============================
   Validate CRC32 fields
   ============================ */

int gpt_validate(cfdisk_state_t* state)
{
    uint8_t hdr_buf[SECTOR_SIZE];

    if (disk_read(state->dev, 1, 1, hdr_buf) != 0)
        return -1;

    gpt_header_t* hdr = (gpt_header_t*)hdr_buf;

    if (hdr->signature != GPT_HEADER_SIGNATURE)
        return -1;

    /* Validate header CRC32 */
    uint32_t saved_crc = hdr->header_crc32;
    hdr->header_crc32  = 0;
    uint32_t computed  = crc32_compute(hdr, hdr->header_size);
    hdr->header_crc32  = saved_crc;

    if (computed != saved_crc)
        return -1;

    return 0;
}

/* ============================
   GPT Read
   ============================ */

int gpt_read(cfdisk_state_t* state)
{
    uint8_t hdr_buf[SECTOR_SIZE];

    if (disk_read(state->dev, 1, 1, hdr_buf) != 0)
        return -1;

    gpt_header_t* hdr = (gpt_header_t*)hdr_buf;

    if (hdr->signature != GPT_HEADER_SIGNATURE)
        return -1;

    state->table_type = TABLE_GPT;
    state->part_count = 0;

    uint32_t num_entries = hdr->num_partition_entries;
    if (num_entries > MAX_GPT_PARTS)
        num_entries = MAX_GPT_PARTS;

    uint64_t entry_lba  = hdr->partition_entry_lba;
    uint32_t entry_size = hdr->sizeof_partition_entry;

    /* Read partition entries sector by sector */
    uint8_t sector[SECTOR_SIZE];
    uint32_t entries_read = 0;

    for (uint32_t lba_off = 0; entries_read < num_entries; lba_off++)
    {
        if (disk_read(state->dev, entry_lba + lba_off, 1, sector) != 0)
            break;

        for (int ei = 0; ei * (int)entry_size < SECTOR_SIZE && entries_read < num_entries; ei++, entries_read++)
        {
            gpt_entry_t* e = (gpt_entry_t*)(sector + ei * entry_size);

            /* Skip empty entries (all-zero type GUID) */
            if (gpt_guid_is_empty(e->type_guid))
                continue;

            if (state->part_count >= MAX_PARTITIONS)
                break;

            partition_t* p = &state->parts[state->part_count++];
            memset(p, 0, sizeof(*p));

            p->start_lba    = e->start_lba;
            p->end_lba      = e->end_lba;
            p->size_sectors = e->end_lba - e->start_lba + 1;
            if (gpt_guid_match(e->type_guid, gpt_guid_efi_system))
                p->type = PART_TYPE_EFI;
            else if (gpt_guid_match(e->type_guid, gpt_guid_grace_branchfs))
                p->type = PART_TYPE_GRACE;
            else
                p->type = PART_TYPE_GPT;
            p->is_gpt       = true;

            /* Convert UTF-16LE name to ASCII (lossy but sufficient) */
            int ni = 0;
            for (int wi = 0; wi < 35 && e->name[wi] != 0 && ni < 35; wi++, ni++)
                p->name[ni] = (char)(e->name[wi] & 0x7F);
            p->name[ni] = '\0';

            if (p->name[0] == '\0')
            {
                p->name[0] = 'p'; p->name[1] = 'a'; p->name[2] = 'r';
                p->name[3] = 't'; p->name[4] = '0' + (char)state->part_count;
                p->name[5] = '\0';
            }
        }
    }

    return 0;
}

/* ============================
   GPT Write (primary + backup)
   ============================ */

int gpt_write(cfdisk_state_t* state)
{
    uint8_t sector[SECTOR_SIZE];

    uint64_t entry_bytes = (uint64_t)MAX_GPT_PARTS * GPT_ENTRY_SIZE;
    uint64_t entry_sectors = entry_bytes / SECTOR_SIZE;
    uint64_t entry_lba = 2;
    uint64_t backup_entry_lba = state->disk.sector_count - 1 - entry_sectors;

    uint8_t entry_table[GPT_ENTRY_SIZE * MAX_GPT_PARTS];
    memset(entry_table, 0, sizeof(entry_table));

    int entry_idx = 0;
    for (int pi = 0; pi < state->part_count && entry_idx < MAX_GPT_PARTS; pi++)
    {
        partition_t* p = &state->parts[pi];
        if (p->is_deleted)
            continue;

        gpt_entry_t* e = (gpt_entry_t*)(entry_table + entry_idx * GPT_ENTRY_SIZE);
        gpt_set_guid(e->type_guid, gpt_guid_for_type(p->type));
        e->start_lba = p->start_lba;
        e->end_lba = p->end_lba;
        e->attributes = 0;

        for (int ni = 0; ni < 35 && p->name[ni] != '\0'; ni++)
            e->name[ni] = (uint16_t)(uint8_t)p->name[ni];

        entry_idx++;
    }

    uint32_t array_crc = crc32_compute(entry_table, (uint32_t)entry_bytes);

    /* Protective MBR */
    memset(sector, 0, SECTOR_SIZE);
    mbr_t* mbr = (mbr_t*)sector;
    mbr->signature = MBR_SIGNATURE;
    mbr->entries[0].type = PART_TYPE_GPT;
    mbr->entries[0].lba_start = 1;
    if (state->disk.sector_count > 0xFFFFFFFFu)
        mbr->entries[0].lba_size = 0xFFFFFFFFu;
    else
        mbr->entries[0].lba_size = (uint32_t)(state->disk.sector_count - 1);
    if (disk_write(state->dev, 0, 1, sector) != 0)
        return -1;

    /* Write primary partition entries */
    for (uint64_t i = 0; i < entry_sectors; i++)
    {
        const uint8_t* src = entry_table + i * SECTOR_SIZE;
        if (disk_write(state->dev, entry_lba + i, 1, src) != 0)
            return -1;
    }

    /* Build primary GPT header at LBA 1 */
    memset(sector, 0, SECTOR_SIZE);
    gpt_header_t* hdr = (gpt_header_t*)sector;
    hdr->signature             = GPT_HEADER_SIGNATURE;
    hdr->revision              = GPT_HEADER_REVISION;
    hdr->header_size           = sizeof(gpt_header_t);
    hdr->my_lba                = 1;
    hdr->alternate_lba         = state->disk.sector_count - 1;
    hdr->first_usable_lba      = 34;
    hdr->last_usable_lba       = state->disk.sector_count - 34;
    hdr->partition_entry_lba   = entry_lba;
    hdr->num_partition_entries = MAX_GPT_PARTS;
    hdr->sizeof_partition_entry= GPT_ENTRY_SIZE;
    hdr->partition_entry_array_crc32 = array_crc;
    hdr->header_crc32 = 0;
    hdr->header_crc32 = crc32_compute(hdr, hdr->header_size);

    if (disk_write(state->dev, 1, 1, sector) != 0)
        return -1;

    /* Write backup partition entries */
    for (uint64_t i = 0; i < entry_sectors; i++)
    {
        const uint8_t* src = entry_table + i * SECTOR_SIZE;
        if (disk_write(state->dev, backup_entry_lba + i, 1, src) != 0)
            return -1;
    }

    /* Build backup GPT header at last LBA */
    memset(sector, 0, SECTOR_SIZE);
    hdr = (gpt_header_t*)sector;
    hdr->signature             = GPT_HEADER_SIGNATURE;
    hdr->revision              = GPT_HEADER_REVISION;
    hdr->header_size           = sizeof(gpt_header_t);
    hdr->my_lba                = state->disk.sector_count - 1;
    hdr->alternate_lba         = 1;
    hdr->first_usable_lba      = 34;
    hdr->last_usable_lba       = state->disk.sector_count - 34;
    hdr->partition_entry_lba   = backup_entry_lba;
    hdr->num_partition_entries = MAX_GPT_PARTS;
    hdr->sizeof_partition_entry= GPT_ENTRY_SIZE;
    hdr->partition_entry_array_crc32 = array_crc;
    hdr->header_crc32 = 0;
    hdr->header_crc32 = crc32_compute(hdr, hdr->header_size);

    if (disk_write(state->dev, state->disk.sector_count - 1, 1, sector) != 0)
        return -1;

    return 0;
}
