// ============================
// GraceOS Kernel Pipe
// Ring-buffer unidirectional IPC
// ============================

#include "pipe.h"
#include "../../lib/libc/string.h"
#include "../log/klog.h"

/* ============================
   Global Pipe Table
   ============================ */

static pipe_t   pipe_table[PIPE_TABLE_SIZE];
static volatile int pipe_lock = 0;

/* ============================
   Lock Helpers
   ============================ */

static inline void lock_pipes(void)
{
    while (__sync_lock_test_and_set(&pipe_lock, 1))
    {
        #ifdef ARCH_ARM64
        __asm__ volatile ("yield");
        #else
        __asm__ volatile ("pause");
        #endif
    }
}

static inline void unlock_pipes(void)
{
    __sync_lock_release(&pipe_lock);
}

/* ============================
   Initialization
   ============================ */

void pipe_init(void)
{
    memset(pipe_table, 0, sizeof(pipe_table));
    klog_init_msg("Pipe subsystem initialized");
}

/* ============================
   Allocation
   ============================ */

int pipe_alloc(void)
{
    lock_pipes();

    for (int i = 0; i < PIPE_TABLE_SIZE; i++)
    {
        if (!pipe_table[i].valid)
        {
            memset(&pipe_table[i], 0, sizeof(pipe_t));
            pipe_table[i].valid   = 1;
            pipe_table[i].readers = 1;
            pipe_table[i].writers = 1;
            unlock_pipes();
            return i;
        }
    }

    unlock_pipes();
    klog_warn("pipe: table full");
    return -1;
}

/* ============================
   Read / Write
   ============================ */

int pipe_read(int pipe_id, void* buf, int len)
{
    if (pipe_id < 0 || pipe_id >= PIPE_TABLE_SIZE)
        return -1;

    pipe_t* p = &pipe_table[pipe_id];

    if (!p->valid)
        return -1;

    lock_pipes();

    if (p->count == 0)
    {
        unlock_pipes();
        /* EOF if no writers left, otherwise no data yet */
        return (p->writers == 0) ? 0 : -2;  /* -2 = would block */
    }

    uint8_t* dst  = (uint8_t*)buf;
    int      read = 0;

    while (read < len && p->count > 0)
    {
        dst[read++] = p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUFFER_SIZE;
        p->count--;
    }

    unlock_pipes();
    return read;
}

int pipe_write(int pipe_id, const void* buf, int len)
{
    if (pipe_id < 0 || pipe_id >= PIPE_TABLE_SIZE)
        return -1;

    pipe_t* p = &pipe_table[pipe_id];

    if (!p->valid || p->readers == 0)
        return -1;  /* Broken pipe */

    lock_pipes();

    const uint8_t* src     = (const uint8_t*)buf;
    int            written = 0;

    while (written < len && p->count < PIPE_BUFFER_SIZE)
    {
        p->buf[p->write_pos] = src[written++];
        p->write_pos = (p->write_pos + 1) % PIPE_BUFFER_SIZE;
        p->count++;
    }

    unlock_pipes();
    return written;
}

/* ============================
   Close Helpers
   ============================ */

void pipe_close_read(int pipe_id)
{
    if (pipe_id < 0 || pipe_id >= PIPE_TABLE_SIZE)
        return;

    lock_pipes();
    pipe_t* p = &pipe_table[pipe_id];
    if (p->valid && p->readers > 0)
        p->readers--;

    if (p->readers == 0 && p->writers == 0)
        p->valid = 0;   /* Both ends closed – free the slot */

    unlock_pipes();
}

void pipe_close_write(int pipe_id)
{
    if (pipe_id < 0 || pipe_id >= PIPE_TABLE_SIZE)
        return;

    lock_pipes();
    pipe_t* p = &pipe_table[pipe_id];
    if (p->valid && p->writers > 0)
        p->writers--;

    if (p->readers == 0 && p->writers == 0)
        p->valid = 0;

    unlock_pipes();
}

/* ============================
   Lookup
   ============================ */

pipe_t* pipe_get(int pipe_id)
{
    if (pipe_id < 0 || pipe_id >= PIPE_TABLE_SIZE)
        return 0;

    pipe_t* p = &pipe_table[pipe_id];
    return p->valid ? p : 0;
}
