// ============================
// GraceOS Shell: sysinfo Command
// Neofetch-style system information
// ============================

#include "../../include/grace/sysinfo.h"
#include "../../lib/libgrace/grace.h"
#include "../../drivers/video/tty.h"
#include "../../drivers/storage/bfs.h"
#include "../../lib/libc/string.h"

extern struct bfs_instance g_bfs;

#define SYSINFO_MIN_STORAGE_BYTES (64ULL * 1024ULL * 1024ULL * 1024ULL)

/* ============================
   Helper: Print number
   ============================ */

static void print_number(uint64_t val)
{
    char buf[24];
    int i = 0;

    if (val == 0) {
        tty_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        tty_putchar(buf[--i]);
    }
}

/* ============================
   ASCII Art Logo Lines
   ============================ */

static const char* logo[] = {
    "    _____                     ____  _____ ",
    "   / ____|                   / __ \\/ ____|",
    "  | |  __ _ __ __ _  ___ ___| |  | | ___  ",
    "  | | |_ | '__/ _` |/ __/ _ \\ |  | |\\___ \\",
    "  | |__| | | | (_| | (_|  __/ |__| |____) ",
    "   \\_____|_|  \\__,_|\\___\\___|\\____/|_____/",
    "                                          ",
};

#define LOGO_LINES 7

/* ============================
   Compute logo width
   ============================ */

static int logo_width(void)
{
    int max = 0;

    for (int i = 0; i < LOGO_LINES; i++) {
        int len = strlen(logo[i]);
        if (len > max)
            max = len;
    }

    return max;
}

/* ============================
   Print info line next to logo
   ============================ */

static void print_info_line(int line, struct grace_sysinfo* info)
{
    uint64_t used_mb, total_mb;

    switch (line) {
    case 0:
        // OS
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("OS:       ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        tty_print("GraceOS 0.1");
        break;

    case 1:
        // Kernel
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("Kernel:   ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        tty_print(info->kernel_name);
        tty_print(" ");
        tty_print(info->kernel_version);
        break;

    case 2:
        // Architecture
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("Arch:     ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        tty_print(info->arch);
        break;

    case 3:
        // Memory
        used_mb = (info->total_mem - info->free_mem) / 1024 / 1024;
        total_mb = info->total_mem / 1024 / 1024;
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("Memory:   ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        print_number(used_mb);
        tty_print(" MB / ");
        print_number(total_mb);
        tty_print(" MB");
        break;

    case 4:
        // Uptime
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("Uptime:   ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        if (info->uptime_ms >= 60000) {
            print_number(info->uptime_ms / 60000);
            tty_print(" min");
        } else if (info->uptime_ms >= 1000) {
            print_number(info->uptime_ms / 1000);
            tty_print(" sec");
        } else {
            print_number(info->uptime_ms);
            tty_print(" ms");
        }
        break;

    case 5:
        // Filesystem + Storage
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("FS:       ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        tty_print(info->fs_name);

        if (g_bfs.root && g_bfs.root->total_pages > 0)
        {
            uint64_t total_bytes = g_bfs.root->total_pages * (uint64_t)BFS_PAGE_SIZE;
            uint64_t used_pages = g_bfs.root->total_pages - g_bfs.root->free_pages;
            uint64_t used_bytes = used_pages * (uint64_t)BFS_PAGE_SIZE;

            if (total_bytes < SYSINFO_MIN_STORAGE_BYTES)
                total_bytes = SYSINFO_MIN_STORAGE_BYTES;

            if (used_bytes > total_bytes)
                used_bytes = total_bytes;

            tty_print(" (");
            print_number(used_bytes / 1024 / 1024);
            tty_print(" MB / ");
            print_number(total_bytes / 1024 / 1024);
            tty_print(" MB)");
        }
        break;

    case 6:
        // Tasks
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print("Tasks:    ");
        tty_set_color(TTY_WHITE, TTY_BLACK);
        print_number(info->process_count);
        tty_print(" running");
        break;

    default:
        break;
    }
}

/* ============================
   sysinfo Command Implementation
   ============================ */

void cmd_sysinfo(void)
{
    struct grace_sysinfo info;

    if (grace_sysinfo(&info) < 0) {
        tty_set_color(TTY_RED, TTY_BLACK);
        tty_print("sysinfo: failed to get system information\n");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        return;
    }

    tty_print("\n");

    // Print logo with info on the right
    int w = logo_width();
    for (int i = 0; i < LOGO_LINES; i++) {
        int len = strlen(logo[i]);

        // Print logo line
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        tty_print(logo[i]);

        // Pad
        for (int s = 0; s < w - len + 3; s++)
            tty_putchar(' ');

        // Print corresponding info
        print_info_line(i, &info);
        tty_print("\n");
    }

    // Color palette
    tty_print("\n  ");
    
    // First row - dark colors (as background blocks)
    tty_set_color(TTY_BLACK, TTY_BLACK);
    tty_print("███");
    tty_set_color(TTY_RED, TTY_RED);
    tty_print("███");
    tty_set_color(TTY_GREEN, TTY_GREEN);
    tty_print("███");
    tty_set_color(TTY_BROWN, TTY_BROWN);
    tty_print("███");
    tty_set_color(TTY_BLUE, TTY_BLUE);
    tty_print("███");
    tty_set_color(TTY_MAGENTA, TTY_MAGENTA);
    tty_print("███");
    tty_set_color(TTY_CYAN, TTY_CYAN);
    tty_print("███");
    tty_set_color(TTY_LIGHT_GREY, TTY_LIGHT_GREY);
    tty_print("███");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("\n");

    // Second row - light colors
    tty_print("  ");
    tty_set_color(TTY_DARK_GREY, TTY_DARK_GREY);
    tty_print("███");
    tty_set_color(TTY_LIGHT_RED, TTY_LIGHT_RED);
    tty_print("███");
    tty_set_color(TTY_LIGHT_GREEN, TTY_LIGHT_GREEN);
    tty_print("███");
    tty_set_color(TTY_YELLOW, TTY_YELLOW);
    tty_print("███");
    tty_set_color(TTY_LIGHT_BLUE, TTY_LIGHT_BLUE);
    tty_print("███");
    tty_set_color(TTY_LIGHT_MAGENTA, TTY_LIGHT_MAGENTA);
    tty_print("███");
    tty_set_color(TTY_LIGHT_CYAN, TTY_LIGHT_CYAN);
    tty_print("███");
    tty_set_color(TTY_WHITE, TTY_WHITE);
    tty_print("███");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("\n\n");

    // Reset color
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
}
