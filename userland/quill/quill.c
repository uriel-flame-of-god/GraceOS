// Quill - simple text editor for GraceOS

#include "quill.h"
#include "../../lib/libc/string.h"
#include "../../drivers/video/tty.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/storage/bfs.h"
#include "../../kernel/log/klog.h"

extern struct bfs_instance g_bfs;

static struct quill_buffer buffer;
static struct quill_cursor cursor;
static char file_data[Q_MAX_LINES * Q_MAX_COLS];
static int  row_offset;
static int  running;

/* Full 25-row text area — no status bars, no chrome */
#define Q_COLS  79   /* max usable columns (leave 1 for safety) */
#define Q_ROWS  25   /* full screen height */

#define DIRTY_NONE  0
#define DIRTY_LINE  1   /* only current line changed */
#define DIRTY_ALL   2   /* full redraw */

static int dirty = DIRTY_ALL;

/* ---- scroll ------------------------------------------------------------ */

static void quill_scroll(void)
{
    if (cursor.y < row_offset)
        row_offset = cursor.y;
    if (cursor.y >= row_offset + Q_ROWS)
        row_offset = cursor.y - Q_ROWS + 1;
    if (row_offset < 0)
        row_offset = 0;
}

/* ---- render ------------------------------------------------------------ */

static void quill_draw_line(int screen_row)
{
    int file_row = row_offset + screen_row;
    tty_set_cursor(0, (size_t)screen_row);
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);

    if (file_row < buffer.line_count) {
        const char* line = buffer.lines[file_row];
        int len = (int)strlen(line);
        int shown = 0;
        for (int i = 0; i < len && shown < Q_COLS; i++, shown++)
            tty_putchar(line[i]);
        while (shown++ < Q_COLS)
            tty_putchar(' ');
    } else {
        for (int i = 0; i < Q_COLS; i++)
            tty_putchar(' ');
    }
}

static void quill_render(void)
{
    if (dirty == DIRTY_NONE)
        return;

    int old_offset = row_offset;
    quill_scroll();

    if (row_offset != old_offset)
        dirty = DIRTY_ALL;

    tty_begin_batch();

    if (dirty == DIRTY_LINE) {
        quill_draw_line(cursor.y - row_offset);
    } else {
        tty_clear();
        for (int r = 0; r < Q_ROWS; r++)
            quill_draw_line(r);
    }

    tty_end_batch();

    /* place blinking hardware cursor */
    tty_set_cursor((size_t)cursor.x, (size_t)(cursor.y - row_offset));
    tty_show_cursor();

    dirty = DIRTY_NONE;
}

/* ---- file I/O ---------------------------------------------------------- */

static void quill_load(const char* filename)
{
    memset(&buffer, 0, sizeof(buffer));
    buffer.line_count = 1;
    if (filename)
        strncpy(buffer.filename, filename, 127);

    if (!filename || !filename[0])
        return;

    uint64_t size = 0;
    int r = bfs_read_file(&g_bfs, filename, file_data, sizeof(file_data), &size);
    if (r == -1) { bfs_create(&g_bfs, filename, 0); return; }
    if (r != 0)  { klog_warn("Quill: load failed"); return; }

    int line = 0, col = 0;
    for (uint64_t i = 0; i < size && line < Q_MAX_LINES; i++) {
        char c = file_data[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (++line >= Q_MAX_LINES) break;
            col = 0;
            if (line + 1 > buffer.line_count) buffer.line_count = line + 1;
            continue;
        }
        if (col < Q_MAX_COLS - 1) {
            buffer.lines[line][col++] = c;
            buffer.lines[line][col]   = '\0';
        }
    }
    buffer.modified = 0;
}

static void quill_save(void)
{
    if (!buffer.filename[0]) return;

    uint64_t size = 0;
    for (int i = 0; i < buffer.line_count; i++) {
        int len = (int)strlen(buffer.lines[i]);
        if (size + (uint64_t)len + 1 >= sizeof(file_data)) break;
        memcpy(&file_data[size], buffer.lines[i], (uint64_t)len);
        size += (uint64_t)len;
        if (i < buffer.line_count - 1)
            file_data[size++] = '\n';
    }
    bfs_write_file(&g_bfs, buffer.filename, file_data, size);
    buffer.modified = 0;
}

/* ---- editing ----------------------------------------------------------- */

static void quill_insert(char c)
{
    if (cursor.y >= Q_MAX_LINES) return;
    char* line = buffer.lines[cursor.y];
    int len = (int)strlen(line);
    if (len >= Q_COLS) return;
    for (int i = len; i >= cursor.x; i--) line[i + 1] = line[i];
    line[cursor.x++] = c;
    buffer.modified = 1;
}

/* returns 1 if a line merge happened */
static int quill_backspace(void)
{
    if (cursor.x == 0 && cursor.y == 0) return 0;
    char* line = buffer.lines[cursor.y];
    if (cursor.x > 0) {
        int len = (int)strlen(line);
        for (int i = cursor.x - 1; i < len; i++) line[i] = line[i + 1];
        cursor.x--;
        buffer.modified = 1;
        return 0;
    }
    /* merge onto previous line */
    char* prev = buffer.lines[cursor.y - 1];
    int prev_len = (int)strlen(prev);
    if (prev_len + (int)strlen(line) < Q_MAX_COLS) {
        strcat(prev, line);
        for (int i = cursor.y; i < buffer.line_count - 1; i++)
            strcpy(buffer.lines[i], buffer.lines[i + 1]);
        buffer.line_count--;
        cursor.y--;
        cursor.x = prev_len;
        buffer.modified = 1;
    }
    return 1;
}

static void quill_enter(void)
{
    if (buffer.line_count >= Q_MAX_LINES) return;
    char* line = buffer.lines[cursor.y];
    int len = (int)strlen(line);
    for (int i = buffer.line_count; i > cursor.y + 1; i--)
        strcpy(buffer.lines[i], buffer.lines[i - 1]);
    if (cursor.x < len) {
        strcpy(buffer.lines[cursor.y + 1], &line[cursor.x]);
        line[cursor.x] = '\0';
    } else {
        buffer.lines[cursor.y + 1][0] = '\0';
    }
    buffer.line_count++;
    cursor.y++;
    cursor.x = 0;
    buffer.modified = 1;
}

static void quill_move(int dx, int dy)
{
    if (dy) {
        cursor.y += dy;
        if (cursor.y < 0) cursor.y = 0;
        if (cursor.y >= buffer.line_count) cursor.y = buffer.line_count - 1;
    }
    if (dx < 0) {
        if (cursor.x > 0) cursor.x--;
        else if (cursor.y > 0) { cursor.y--; cursor.x = (int)strlen(buffer.lines[cursor.y]); }
    } else if (dx > 0) {
        int len = (int)strlen(buffer.lines[cursor.y]);
        if (cursor.x < len) cursor.x++;
        else if (cursor.y + 1 < buffer.line_count) { cursor.y++; cursor.x = 0; }
    }
    int llen = (int)strlen(buffer.lines[cursor.y]);
    if (cursor.x > llen) cursor.x = llen;
}

/* ---- main -------------------------------------------------------------- */

void quill_run(const char* filename)
{
    klog_init_msg("Starting Quill");
    quill_load(filename);
    cursor.x = 0; cursor.y = 0; row_offset = 0;
    dirty = DIRTY_ALL;
    running = 1;

    while (running) {
        quill_render();
        unsigned char key = (unsigned char)keyboard_getchar();

        if (key == KEY_CTRL_X) {
            if (!buffer.modified || !buffer.filename[0]) { running = 0; break; }
            /* ask to save */
            tty_set_cursor(0, Q_ROWS - 1);
            tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            tty_print("Save before exit? (y/n) ");
            unsigned char ans = (unsigned char)keyboard_getchar();
            if (ans == 'y' || ans == 'Y') quill_save();
            running = 0;
        }
        else if (key == KEY_CTRL_S) {
            quill_save();
            /* flash "Saved" on last row for one render cycle */
            tty_set_cursor(0, Q_ROWS - 1);
            tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            tty_print("Saved.                  ");
            dirty = DIRTY_ALL;
        }
        else if (key == KEY_BACKSPACE || key == '\b' || key == 0x7F) {
            dirty = quill_backspace() ? DIRTY_ALL : DIRTY_LINE;
        }
        else if (key == KEY_ENTER || key == '\n' || key == '\r') {
            quill_enter();
            dirty = DIRTY_ALL;
        }
        else if (key == KEY_SPECIAL_UP)    { quill_move(0, -1);      dirty = DIRTY_ALL; }
        else if (key == KEY_SPECIAL_DOWN)  { quill_move(0,  1);      dirty = DIRTY_ALL; }
        else if (key == KEY_SPECIAL_LEFT)  { quill_move(-1, 0);      dirty = DIRTY_LINE; }
        else if (key == KEY_SPECIAL_RIGHT) { quill_move(1,  0);      dirty = DIRTY_LINE; }
        else if (key == KEY_SPECIAL_HOME)  { cursor.x = 0;           dirty = DIRTY_LINE; }
        else if (key == KEY_SPECIAL_END)   { cursor.x = (int)strlen(buffer.lines[cursor.y]); dirty = DIRTY_LINE; }
        else if (key == KEY_SPECIAL_PGUP)  { quill_move(0, -Q_ROWS); dirty = DIRTY_ALL; }
        else if (key == KEY_SPECIAL_PGDN)  { quill_move(0,  Q_ROWS); dirty = DIRTY_ALL; }
        else if (key >= 32 && key < 127)   { quill_insert(key);      dirty = DIRTY_LINE; }
    }

    tty_clear();
    tty_set_cursor(0, 0);
    tty_show_cursor();
    klog_log("Quill closed");
}
