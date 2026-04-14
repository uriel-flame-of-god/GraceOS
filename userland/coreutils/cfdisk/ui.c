// ============================
// GraceOS cfdisk — TUI via syscalls
// Uses sys_fb_* and sys_write for rendering
// ============================

#include "ui.h"
#include "input.h"
#include "../../../lib/libc/string.h"

/* ============================
   Syscall wrappers
   ============================ */

#define SYS_WRITE       1
#define SYS_FB_CLEAR   63
#define SYS_FB_RECT    65
#define SYS_FB_PRESENT 66

/* Colors (packed ARGB/BGR depending on framebuffer — use ARGB32) */
#define COLOR_BG       0xFF1A1A2E
#define COLOR_HEADER   0xFF0F3460
#define COLOR_SELECTED 0xFFE94560
#define COLOR_FREE     0xFF2C3E50
#define COLOR_TEXT     0xFFECECEC
#define COLOR_STATUS   0xFF34495E

static void syscall2(long num, long a1, long a2)
{
    __asm__ volatile (
        "mov %0, %%rax\n"
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "int $0x80\n"
        : : "r"(num), "r"(a1), "r"(a2) : "rax", "rdi", "rsi"
    );
}

static void syscall5(long num, long a1, long a2, long a3, long a4, long a5)
{
    __asm__ volatile (
        "mov %0, %%rax\n"
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov %4, %%r10\n"
        "mov %5, %%r8\n"
        "int $0x80\n"
        : : "r"(num), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
        : "rax", "rdi", "rsi", "rdx", "r10", "r8"
    );
}

static void fb_clear(uint32_t color)
{
    syscall2(SYS_FB_CLEAR, (long)color, 0);
}

static void fb_rect(int x, int y, int w, int h, uint32_t color)
{
    syscall5(SYS_FB_RECT, (long)x, (long)y, (long)w, (long)h, (long)color);
}

static void fb_present(void)
{
    syscall2(SYS_FB_PRESENT, 0, 0);
}

static void tty_write(const char* s)
{
    long len = (long)strlen(s);
    __asm__ volatile (
        "mov %0, %%rax\n"
        "mov $1, %%rdi\n"  /* fd=1 stdout */
        "mov %1, %%rsi\n"
        "mov %2, %%rdx\n"
        "int $0x80\n"
        : : "r"((long)SYS_WRITE), "r"((long)s), "r"(len)
        : "rax", "rdi", "rsi", "rdx"
    );
}

/* ============================
   Number formatting helpers
   ============================ */

static void fmt_uint(char* buf, uint64_t v)
{
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[24]; int ti = 0;
    while (v > 0) { tmp[ti++] = '0' + (int)(v % 10); v /= 10; }
    int bi = 0;
    for (int k = ti - 1; k >= 0; k--) buf[bi++] = tmp[k];
    buf[bi] = '\0';
}

static void fmt_size_mb(char* buf, uint64_t sectors)
{
    uint64_t mb = (sectors * 512) / (1024 * 1024);
    fmt_uint(buf, mb);
    int len = (int)strlen(buf);
    buf[len] = 'M'; buf[len+1] = 'B'; buf[len+2] = '\0';
}

/* ============================
   Main UI Draw
   ============================ */

void ui_draw(cfdisk_state_t* state)
{
    fb_clear(COLOR_BG);

    /* Header bar */
    fb_rect(0, 0, 800, 20, COLOR_HEADER);

    /* Partition bars — simple colored blocks, one per partition */
    int bar_y = 40;
    for (int i = 0; i < state->part_count; i++)
    {
        partition_t* p = &state->parts[i];
        if (p->is_deleted)
            continue;

        uint32_t color = (i == state->selected) ? COLOR_SELECTED : COLOR_FREE;
        fb_rect(10, bar_y + i * 24, 780, 20, color);
    }

    fb_present();

    /* Text mode overlay — write partition table to tty */
    tty_write("\033[H\033[2J");  /* Clear terminal if TTY supports it */
    tty_write("cfdisk  --  GraceOS Partition Manager\n");
    tty_write("--------------------------------------\n");

    char buf[128];
    char nbuf[24];
    char sbuf[16];

    for (int i = 0; i < state->part_count; i++)
    {
        partition_t* p = &state->parts[i];
        if (p->is_deleted)
            continue;

        /* Mark selected */
        if (i == state->selected)
            tty_write(" > ");
        else
            tty_write("   ");

        /* Name */
        tty_write(p->name);
        tty_write("  ");

        /* Start LBA */
        fmt_uint(nbuf, p->start_lba);
        tty_write(nbuf);
        tty_write(" - ");

        /* End LBA */
        fmt_uint(nbuf, p->end_lba);
        tty_write(nbuf);
        tty_write("  ");

        /* Size */
        fmt_size_mb(sbuf, p->size_sectors);
        tty_write(sbuf);

        if (p->is_new)
            tty_write("  [new]");

        tty_write("\n");
        (void)buf;
    }

    tty_write("\n[n]ew  [d]elete  [g]pt+efi  [w]rite  [q]uit  [up/down] select\n");

    if (state->modified)
        tty_write("  * Unsaved changes\n");
}

void ui_status(const char* msg)
{
    tty_write("\nSTATUS: ");
    tty_write(msg);
    tty_write("\n");
}

int ui_confirm(const char* prompt)
{
    tty_write(prompt);
    tty_write(" [y/N] ");
    int key = input_getkey();
    tty_write("\n");
    return (key == 'y' || key == 'Y') ? 1 : 0;
}
