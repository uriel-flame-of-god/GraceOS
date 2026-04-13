/*
 * llama-io.c — BranchFS and in-memory I/O backends for the LLM runtime
 *
 * The BranchFS backend reads the entire file into a SASY segment once (eager
 * load) and then serves reads from that buffer.  This avoids repeated BranchFS
 * tree traversals during tensor page-in and keeps latency predictable.
 * For very large models the llama-mmap layer handles demand-paging instead.
 */

#include "llama-io.h"
#include "llama-impl.h"
#include "../mm/kheap.h"
#include "../mm/sasy/sasy.h"
#include "../../drivers/storage/bfs.h"
#include "../log/klog.h"

/* Forward declaration of the global BranchFS instance (defined in bfs.c or
 * the kernel init path).  The LLM runtime uses read-only access only. */
extern struct bfs_instance g_bfs;

/* =========================================================================
 * BranchFS-backed reader (eager load into SASY segment)
 * ========================================================================= */

typedef struct {
    seg_handle_t seg;      /* SASY segment holding file contents */
    uint8_t*     base;     /* sasy_lock() result (kept locked during use) */
    uint64_t     file_sz;
    uint64_t     pos;
} bfs_read_priv_t;

static int bfs_read_fn(llm_io_read_t* io, void* dst, size_t n)
{
    bfs_read_priv_t* p = (bfs_read_priv_t*)io->priv;
    if (p->pos + n > p->file_sz) {
        klog_error("[llm-io] read past end of file");
        return -1;
    }
    llm_copy(dst, p->base + p->pos, n);
    p->pos += n;
    return 0;
}

static int bfs_seek_fn(llm_io_read_t* io, uint64_t offset)
{
    bfs_read_priv_t* p = (bfs_read_priv_t*)io->priv;
    if (offset > p->file_sz) return -1;
    p->pos = offset;
    return 0;
}

static uint64_t bfs_tell_fn(llm_io_read_t* io)
{
    return ((bfs_read_priv_t*)io->priv)->pos;
}

static uint64_t bfs_size_fn(llm_io_read_t* io)
{
    return ((bfs_read_priv_t*)io->priv)->file_sz;
}

static void bfs_close_fn(llm_io_read_t* io)
{
    bfs_read_priv_t* p = (bfs_read_priv_t*)io->priv;
    if (p->seg != INVALID_HANDLE) {
        sasy_unlock(p->seg);
        sasy_free(p->seg);
        p->seg = INVALID_HANDLE;
    }
    kfree(p);
    kfree(io);
}

llm_io_read_t* llm_io_open_file(const char* path)
{
    uint64_t file_sz = 0;
    if (bfs_file_size(&g_bfs, path, &file_sz) != 0 || file_sz == 0) {
        klog_error("[llm-io] file not found or empty");
        return (llm_io_read_t*)0;
    }

    /* Allocate a SASY segment to hold the entire file */
    seg_handle_t seg = sasy_create(file_sz, SEG_DATA_AUTO,
                                   SEG_FLAG_READ | SEG_FLAG_NOSWAP);
    if (seg == INVALID_HANDLE) {
        klog_error("[llm-io] SASY allocation failed");
        return (llm_io_read_t*)0;
    }

    void* base = sasy_lock(seg);
    if (!base) {
        sasy_free(seg);
        klog_error("[llm-io] SASY lock failed");
        return (llm_io_read_t*)0;
    }

    uint64_t actual = 0;
    if (bfs_read_file(&g_bfs, path, base, file_sz, &actual) != 0 ||
        actual != file_sz) {
        sasy_unlock(seg);
        sasy_free(seg);
        klog_error("[llm-io] file read failed");
        return (llm_io_read_t*)0;
    }

    bfs_read_priv_t* priv = (bfs_read_priv_t*)kmalloc(sizeof(*priv));
    llm_io_read_t*   io   = (llm_io_read_t*)  kmalloc(sizeof(*io));
    if (!priv || !io) {
        if (priv) kfree(priv);
        if (io)   kfree(io);
        sasy_unlock(seg);
        sasy_free(seg);
        klog_error("[llm-io] kmalloc failed");
        return (llm_io_read_t*)0;
    }

    priv->seg     = seg;
    priv->base    = (uint8_t*)base;
    priv->file_sz = file_sz;
    priv->pos     = 0;

    io->read  = bfs_read_fn;
    io->seek  = bfs_seek_fn;
    io->tell  = bfs_tell_fn;
    io->size  = bfs_size_fn;
    io->close = bfs_close_fn;
    io->priv  = priv;

    return io;
}

/* =========================================================================
 * In-memory reader
 * ========================================================================= */

typedef struct {
    const uint8_t* base;
    uint64_t       len;
    uint64_t       pos;
} mem_read_priv_t;

static int mem_read_fn(llm_io_read_t* io, void* dst, size_t n)
{
    mem_read_priv_t* p = (mem_read_priv_t*)io->priv;
    if (p->pos + n > p->len) return -1;
    llm_copy(dst, p->base + p->pos, n);
    p->pos += n;
    return 0;
}

static int mem_seek_fn(llm_io_read_t* io, uint64_t offset)
{
    mem_read_priv_t* p = (mem_read_priv_t*)io->priv;
    if (offset > p->len) return -1;
    p->pos = offset;
    return 0;
}

static uint64_t mem_tell_fn(llm_io_read_t* io)  { return ((mem_read_priv_t*)io->priv)->pos; }
static uint64_t mem_size_fn(llm_io_read_t* io)  { return ((mem_read_priv_t*)io->priv)->len; }
static void     mem_close_fn(llm_io_read_t* io)
{
    kfree(io->priv);
    kfree(io);
}

llm_io_read_t* llm_io_open_mem(const void* data, size_t len)
{
    mem_read_priv_t* priv = (mem_read_priv_t*)kmalloc(sizeof(*priv));
    llm_io_read_t*   io   = (llm_io_read_t*)  kmalloc(sizeof(*io));
    if (!priv || !io) {
        if (priv) kfree(priv);
        if (io)   kfree(io);
        return (llm_io_read_t*)0;
    }
    priv->base = (const uint8_t*)data;
    priv->len  = (uint64_t)len;
    priv->pos  = 0;
    io->read  = mem_read_fn;
    io->seek  = mem_seek_fn;
    io->tell  = mem_tell_fn;
    io->size  = mem_size_fn;
    io->close = mem_close_fn;
    io->priv  = priv;
    return io;
}

/* =========================================================================
 * BranchFS writer
 * ========================================================================= */

typedef struct {
    char     path[64];
    uint8_t* buf;
    uint64_t cap;
    uint64_t pos;
} bfs_write_priv_t;

#define BFS_WRITE_INIT_CAP  4096

static int bfs_write_fn(llm_io_write_t* io, const void* src, size_t n)
{
    bfs_write_priv_t* p = (bfs_write_priv_t*)io->priv;
    /* Dynamic growth not available without realloc; use fixed buffer */
    if (p->pos + n > p->cap) {
        klog_error("[llm-io] write buffer overflow");
        return -1;
    }
    llm_copy(p->buf + p->pos, src, n);
    p->pos += n;
    return 0;
}

static uint64_t bfs_write_tell_fn(llm_io_write_t* io)
{
    return ((bfs_write_priv_t*)io->priv)->pos;
}

static int bfs_write_close_fn(llm_io_write_t* io)
{
    bfs_write_priv_t* p = (bfs_write_priv_t*)io->priv;
    int rc = bfs_write_file(&g_bfs, p->path, p->buf, p->pos);
    kfree(p->buf);
    kfree(p);
    kfree(io);
    return rc;
}

llm_io_write_t* llm_io_open_write(const char* path)
{
    bfs_write_priv_t* priv  = (bfs_write_priv_t*)kmalloc(sizeof(*priv));
    llm_io_write_t*   io    = (llm_io_write_t*)  kmalloc(sizeof(*io));
    uint8_t*          buf   = (uint8_t*)          kmalloc(BFS_WRITE_INIT_CAP);
    if (!priv || !io || !buf) {
        if (priv) kfree(priv);
        if (io)   kfree(io);
        if (buf)  kfree(buf);
        return (llm_io_write_t*)0;
    }
    /* Copy path safely */
    int i = 0;
    while (path[i] && i < 63) { priv->path[i] = path[i]; i++; }
    priv->path[i] = '\0';
    priv->buf = buf;
    priv->cap = BFS_WRITE_INIT_CAP;
    priv->pos = 0;
    io->write = bfs_write_fn;
    io->tell  = bfs_write_tell_fn;
    io->close = bfs_write_close_fn;
    io->priv  = priv;
    return io;
}

/* =========================================================================
 * Typed read helpers
 * ========================================================================= */

int llm_io_read_u8 (llm_io_read_t* io, uint8_t*  v) { return llm_io_read(io, v, 1); }
int llm_io_read_u16(llm_io_read_t* io, uint16_t* v) { return llm_io_read(io, v, 2); }
int llm_io_read_u32(llm_io_read_t* io, uint32_t* v) { return llm_io_read(io, v, 4); }
int llm_io_read_u64(llm_io_read_t* io, uint64_t* v) { return llm_io_read(io, v, 8); }
int llm_io_read_i32(llm_io_read_t* io, int32_t*  v) { return llm_io_read(io, v, 4); }
int llm_io_read_i64(llm_io_read_t* io, int64_t*  v) { return llm_io_read(io, v, 8); }
int llm_io_read_f32(llm_io_read_t* io, float*    v) { return llm_io_read(io, v, 4); }

int64_t llm_io_read_str(llm_io_read_t* io, char* buf, size_t buf_max)
{
    uint64_t slen = 0;
    if (llm_io_read_u64(io, &slen) != 0) return -1;

    uint64_t to_read  = slen;
    uint64_t to_store = slen < buf_max - 1 ? slen : buf_max - 1;
    uint64_t skip     = slen - to_store;

    if (llm_io_read(io, buf, (size_t)to_store) != 0) return -1;
    buf[to_store] = '\0';

    /* Skip excess bytes */
    while (skip > 0) {
        uint8_t tmp[64];
        size_t   chunk = skip > 64 ? 64 : (size_t)skip;
        if (llm_io_read(io, tmp, chunk) != 0) return -1;
        skip -= chunk;
    }

    return (int64_t)to_read;
}
