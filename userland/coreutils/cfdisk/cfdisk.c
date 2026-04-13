// ============================
// GraceOS cfdisk — Main Entry Point
// Userland partition manager (syscall-based)
// ============================

#include "cfdisk.h"
#include "disk.h"
#include "mbr.h"
#include "gpt.h"
#include "ui.h"
#include "input.h"
#include "operations.h"
#include "../../../lib/libc/string.h"

/* ============================
   Internal: read partition table
   ============================ */

static int load_table(cfdisk_state_t* state)
{
    /* Try MBR first */
    int r = mbr_read(state);

    if (r == 1)
    {
        /* Disk has GPT protective entry — load GPT */
        return gpt_read(state);
    }

    return r;
}

/* ============================
   cfdisk_run — main loop
   ============================ */

void cfdisk_run(int dev)
{
    cfdisk_state_t state;
    memset(&state, 0, sizeof(state));

    state.dev      = dev;
    state.running  = true;
    state.selected = 0;

    /* Get disk info */
    if (disk_get_info(dev, &state.disk) != 0)
    {
        ui_status("Error: could not get disk info");
        return;
    }

    /* Load partition table */
    if (load_table(&state) != 0)
    {
        /* No valid table found — initialize empty MBR session */
        state.table_type = TABLE_MBR;
        state.part_count = 0;
    }

    ops_rebuild_free(&state);

    /* ============================
       Main Event Loop
       ============================ */

    while (state.running)
    {
        ui_draw(&state);

        int key = input_getkey();

        switch (key)
        {
        case 'q':
        case 'Q':
            if (state.modified)
            {
                if (!ui_confirm("Unsaved changes. Quit anyway?"))
                    break;
            }
            state.running = false;
            break;

        case 'n':
        case 'N':
        {
            /* New partition: default 1 GiB or 1/4 of remaining free */
            uint64_t default_size = 2048 * 1024; /* 1 GiB in sectors */
            if (state.free_count > 0 && state.free[0].size_sectors < default_size)
                default_size = state.free[0].size_sectors;

            if (ops_new_partition(&state, default_size, PART_TYPE_LINUX) != 0)
                ui_status("Error: no free space for new partition");
            else
                ui_status("Partition created (not yet written)");
            break;
        }

        case 'd':
        case 'D':
            if (ops_delete_partition(&state) != 0)
                ui_status("Error: no partition selected");
            else
                ui_status("Partition marked for deletion (not yet written)");
            break;

        case 'w':
        case 'W':
            if (!ui_confirm("Write partition table to disk?"))
                break;
            if (ops_commit(&state) != 0)
                ui_status("Error: write failed");
            else
                ui_status("Partition table written");
            break;

        case 'g':
        case 'G':
            if (!ui_confirm("Create GPT with EFI + BranchFS partitions?"))
                break;
            if (ops_make_efi_branchfs(&state, 512) != 0)
                ui_status("Error: could not create GPT layout");
            else
                ui_status("GPT EFI+BranchFS layout created (not yet written)");
            break;

        case KEY_UP:
            ops_select_prev(&state);
            break;

        case KEY_DOWN:
            ops_select_next(&state);
            break;

        default:
            break;
        }
    }
}
