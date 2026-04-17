// ============================
// GraceOS Unified Logger Service
// Background logging — drains klog ring buffer every 5 seconds,
// writes timestamped batches to BranchFS system/log.txt.
//
// Architecture:
//   - Registered with minit; spawned as a kernel-mode process.
//   - minit handles start / crash-restart / exhaustion.
//   - Uses kernel heap (kmalloc) for state and buffer — simple and robust.
//   - Sleep is implemented via current->sleep_until + sched_block();
//     proc_check_sleepers() (1000 Hz timer) performs the wakeup.
// ============================

#include "logger.h"
#include "../../log/klog.h"
#include "../../../drivers/storage/bfs.h"
#include "../../mm/kheap.h"
#include "../../include/time.h"
#include "../proc.h"
#include "../sched.h"
#include "../../../lib/libc/string.h"

/* ================================================================
   Configuration
   ================================================================ */

#define LOGGER_INTERVAL_MS   5000          /* Wake-up interval in milliseconds */
#define LOGGER_BUF_SIZE      (8 * 1024)    /* SASY accumulation buffer (8 KB)  */
#define LOGGER_LOG_FILE      "system/log.txt"
#define LOGGER_LOG_MAX_BYTES (7 * 1024)    /* Rotate accumulation buf at 7 KB  */
#define LOGGER_STATE_MAGIC   0x4C4F4731U   /* "LOG1" sentinel                  */
#define LOGGER_FILE_MAX_SIZE (1024 * 1024) /* 1 MB per log file                */
#define LOGGER_FILE_MAX_AGE_DAYS 7         /* Delete logs older than 7 days    */
#define LOGGER_MAX_ROTATED_FILES 10        /* Keep up to 10 rotated log files  */

/* ================================================================
   Persistent state — allocated on heap on first run.
   ================================================================ */

typedef struct {
    uint32_t magic;          /* LOGGER_STATE_MAGIC when valid             */
    uint64_t ring_read_pos;  /* Next byte to read from klog ring buffer   */
    uint32_t buf_fill;       /* Valid bytes currently in accumulation buf  */
    uint64_t current_file_size;  /* Current size of active log file       */
    uint64_t last_cleanup_time;  /* Last time cleanup was performed       */
} logger_state_t;

/* Heap-allocated globals; initialised once on first run. */
static logger_state_t* g_state = NULL;
static char*           g_buf   = NULL;

extern struct bfs_instance g_bfs;
extern process_t*          current;

/* ================================================================
   Minimal integer formatters (no printf in kernel)
   ================================================================ */

static void fmt2(char* out, int v)
{
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = '\0';
}

static void fmt4(char* out, int v)
{
    out[0] = (char)('0' + (v / 1000) % 10);
    out[1] = (char)('0' + (v / 100)  % 10);
    out[2] = (char)('0' + (v / 10)   % 10);
    out[3] = (char)('0' + v % 10);
    out[4] = '\0';
}

/* ================================================================
   Build a "[YYYY-MM-DD HH:MM:SS] " header into buf.
   Uses the kernel realtime clock (system_time via get_time()).
   Returns number of characters written.
   ================================================================ */

static int logger_make_timestamp(char* buf, int cap)
{
    if (cap < 24)
        return 0;

    struct timespec ts;
    get_time(&ts);

    time_t secs = ts.tv_sec;

    /* Decompose Unix timestamp into calendar fields */
    int sec  = (int)(secs % 60); secs /= 60;
    int min  = (int)(secs % 60); secs /= 60;
    int hour = (int)(secs % 24); secs /= 24;
    int days = (int)secs;  /* days since 1970-01-01 */

    int year = 1970;
    while (1) {
        int dy = is_leap_year(year) ? 366 : 365;
        if (days < dy) break;
        days -= dy;
        year++;
        if (year > 2100) break;  /* sanity guard */
    }

    int month = 1;
    while (month <= 12) {
        int dm = days_in_month(month, year);
        if (days < dm) break;
        days -= dm;
        month++;
    }
    int day = days + 1;

    char y4[5], mo2[3], d2[3], h2[3], mi2[3], s2[3];
    fmt4(y4, year);
    fmt2(mo2, month);
    fmt2(d2, day);
    fmt2(h2, hour);
    fmt2(mi2, min);
    fmt2(s2, sec);

    int n = 0;
    buf[n++] = '[';
    buf[n++] = y4[0]; buf[n++] = y4[1]; buf[n++] = y4[2]; buf[n++] = y4[3];
    buf[n++] = '-';
    buf[n++] = mo2[0]; buf[n++] = mo2[1];
    buf[n++] = '-';
    buf[n++] = d2[0]; buf[n++] = d2[1];
    buf[n++] = ' ';
    buf[n++] = h2[0]; buf[n++] = h2[1];
    buf[n++] = ':';
    buf[n++] = mi2[0]; buf[n++] = mi2[1];
    buf[n++] = ':';
    buf[n++] = s2[0]; buf[n++] = s2[1];
    buf[n++] = ']';
    buf[n++] = ' ';
    buf[n]   = '\0';
    return n;
}

/* ================================================================
   Append src_len bytes from src into the accumulation buffer.
   Returns 0 on success, -1 if there is no room (caller must rotate
   before calling again).
   ================================================================ */

static int accum_append(char* accum, uint32_t* fill, int cap,
                        const char* src, int src_len)
{
    if ((int)*fill + src_len > cap)
        return -1;
    memcpy(accum + *fill, src, (size_t)src_len);
    *fill = (uint32_t)((int)*fill + src_len);
    return 0;
}

/* ================================================================
   Rotate log files: move system/log.txt -> system/log.1.txt, etc.
   ================================================================ */

static void logger_rotate_files(void)
{
    char old_name[64];
    char new_name[64];
    int i;

    /* Delete the oldest rotated file if it exists */
    for (i = 0; i < 3; i++) old_name[i] = "system/log."[i];
    old_name[11] = '0' + LOGGER_MAX_ROTATED_FILES / 10;
    old_name[12] = '0' + LOGGER_MAX_ROTATED_FILES % 10;
    old_name[13] = '.';
    old_name[14] = 't';
    old_name[15] = 'x';
    old_name[16] = 't';
    old_name[17] = '\0';
    (void)bfs_delete(&g_bfs, old_name);

    /* Rotate files backwards: log.9 <- log.8 <- ... <- log.1 <- log.txt */
    for (i = LOGGER_MAX_ROTATED_FILES - 1; i >= 1; i--)
    {
        /* Build old_name: system/log.(i-1).txt */
        memcpy(old_name, "system/log.", 11);
        if (i - 1 >= 10) {
            old_name[11] = '0' + (i - 1) / 10;
            old_name[12] = '0' + (i - 1) % 10;
            old_name[13] = '.';
        } else {
            old_name[11] = '0' + (i - 1);
            old_name[12] = '.';
            old_name[13] = 't';
        }
        old_name[14] = 'x';
        old_name[15] = 't';
        old_name[16] = '\0';

        /* Build new_name: system/log.i.txt */
        memcpy(new_name, "system/log.", 11);
        if (i >= 10) {
            new_name[11] = '0' + i / 10;
            new_name[12] = '0' + i % 10;
            new_name[13] = '.';
        } else {
            new_name[11] = '0' + i;
            new_name[12] = '.';
            new_name[13] = 't';
        }
        new_name[14] = 'x';
        new_name[15] = 't';
        new_name[16] = '\0';

        /* Read old file and write to new location */
        uint64_t file_size = 0;
        if (bfs_file_size(&g_bfs, old_name, &file_size) == 0 && file_size > 0)
        {
            char* temp_buf = (char*)kmalloc((size_t)file_size + 1);
            if (temp_buf)
            {
                uint64_t read_size = 0;
                if (bfs_read_file(&g_bfs, old_name, temp_buf, file_size, &read_size) == 0)
                {
                    bfs_write_file(&g_bfs, new_name, temp_buf, read_size);
                }
                kfree(temp_buf);
            }
        }
    }

    /* Delete the current log.txt to start fresh */
    (void)bfs_delete(&g_bfs, LOGGER_LOG_FILE);
}

/* ================================================================
   Cleanup old log files (older than LOGGER_FILE_MAX_AGE_DAYS)
   Called periodically to prevent disk bloat.
   ================================================================ */

static void logger_cleanup_old_files(void)
{
    /* Delete rotated log files beyond retention limit */
    char fname[64];
    int i;

    for (i = LOGGER_MAX_ROTATED_FILES + 1; i <= LOGGER_MAX_ROTATED_FILES + 5; i++)
    {
        memcpy(fname, "system/log.", 11);
        if (i >= 100) {
            fname[11] = '0' + i / 100;
            fname[12] = '0' + (i / 10) % 10;
            fname[13] = '0' + i % 10;
            fname[14] = '.';
            fname[15] = 't';
            fname[16] = 'x';
            fname[17] = 't';
            fname[18] = '\0';
        } else if (i >= 10) {
            fname[11] = '0' + i / 10;
            fname[12] = '0' + i % 10;
            fname[13] = '.';
            fname[14] = 't';
            fname[15] = 'x';
            fname[16] = 't';
            fname[17] = '\0';
        } else {
            fname[11] = '0' + i;
            fname[12] = '.';
            fname[13] = 't';
            fname[14] = 'x';
            fname[15] = 't';
            fname[16] = '\0';
        }
        (void)bfs_delete(&g_bfs, fname);
    }
}

/* ================================================================
   Flush the accumulation buffer to BranchFS with file rotation.
   ================================================================ */

static void logger_flush_with_rotation(const char* buf, uint32_t len)
{
    if (len == 0)
        return;

    /* Check if rotation is needed */
    if (g_state->current_file_size + len >= LOGGER_FILE_MAX_SIZE)
    {
        klog_warn("LOGGER: rotating log file (1 MB limit reached)");
        logger_rotate_files();
        g_state->current_file_size = 0;
    }

    /* Ensure file exists with at least LOGGER_LOG_MAX_BYTES allocated */
    (void)bfs_create(&g_bfs, LOGGER_LOG_FILE, (uint64_t)LOGGER_LOG_MAX_BYTES);

    /* Append to file instead of overwriting */
    if (bfs_write_file(&g_bfs, LOGGER_LOG_FILE, buf, (uint64_t)len) == 0)
    {
        g_state->current_file_size += len;
    }
    else
    {
        klog_warn("LOGGER: bfs_write_file failed");
    }

    /* Periodically cleanup old log files */
    uint64_t now_ms = timer_get_ms();
    if (now_ms - g_state->last_cleanup_time > (60000 * 60))  /* Every 60 minutes */
    {
        logger_cleanup_old_files();
        g_state->last_cleanup_time = now_ms;
    }
}

/* ================================================================
   logger_service_main
   Entry point called by minit_kernel_trampoline.
   ================================================================ */

void logger_service_main(void)
{
    klog_init_msg("LOGGER: service starting");

    /* ---- Allocate kernel heap buffers on first run ---- */

    if (!g_state)
    {
        g_state = (logger_state_t*)kmalloc(sizeof(logger_state_t));
        if (!g_state)
        {
            klog_error("LOGGER: failed to allocate state buffer");
            return;
        }
        memset(g_state, 0, sizeof(logger_state_t));
        g_state->magic = LOGGER_STATE_MAGIC;
    }

    if (!g_buf)
    {
        g_buf = (char*)kmalloc(LOGGER_BUF_SIZE);
        if (!g_buf)
        {
            klog_error("LOGGER: failed to allocate log buffer");
            return;
        }
        memset(g_buf, 0, LOGGER_BUF_SIZE);
    }

    klog_init_msg("LOGGER: background service active (5 s interval)");

    /* ================================================================
       Main service loop
       Sleep LOGGER_INTERVAL_MS ms → drain ring → format → flush to BFS.
       ================================================================ */

    while (1)
    {
        /* ---- Sleep for 5 seconds ---- */
        if (current)
        {
            current->sleep_until = timer_get_ms() + (uint64_t)LOGGER_INTERVAL_MS;
            sched_block();   /* scheduler resumes us after sleep_until */
        }

        /* ---- Read new entries from klog ring buffer ---- */

        /*
         * Temporary read area: modest scratch buffer for one tick's log entries.
         */
        char scratch[1024];  /* max bytes drained per tick */

        int new_len = klog_read_new(&g_state->ring_read_pos,
                                    scratch, (int)sizeof(scratch));

        if (new_len > 0)
        {
            /* Build timestamp header for this batch */
            char ts[28];
            int  ts_len = logger_make_timestamp(ts, (int)sizeof(ts));

            /* Separator line: "[timestamp] --- batch ---\n" */
            char sep[64];
            int  sep_len = 0;
            memcpy(sep, ts, (size_t)ts_len); sep_len += ts_len;
            const char* dash = "--- batch ---\n";
            int dlen = 0; while (dash[dlen]) dlen++;
            memcpy(sep + sep_len, dash, (size_t)dlen); sep_len += dlen;

            /* Rotate if accumulation buffer is near full */
            if ((int)g_state->buf_fill + sep_len + new_len > LOGGER_LOG_MAX_BYTES)
            {
                /* Flush what we have, then reset */
                logger_flush_with_rotation(g_buf, g_state->buf_fill);
                g_state->buf_fill = 0;
                klog_warn("LOGGER: log rotated (buffer full)");
            }

            /* Append separator + new content */
            if (accum_append(g_buf, &g_state->buf_fill, LOGGER_BUF_SIZE,
                             sep, sep_len) == 0)
            {
                accum_append(g_buf, &g_state->buf_fill, LOGGER_BUF_SIZE,
                             scratch, new_len);
            }

            /* Write accumulated content to BFS */
            logger_flush_with_rotation(g_buf, g_state->buf_fill);
        }
    }
}
