#ifndef GRACE_LLM_IO_H
#define GRACE_LLM_IO_H

/*
 * llama-io.h — Abstract streaming I/O interface for the GraceOS LLM runtime
 *
 * Provides a thin abstraction over BranchFS file operations so that model
 * loading code is not coupled to a specific storage backend.
 *
 * Two built-in implementations are provided:
 *   llm_io_file  — BranchFS-backed sequential reader
 *   llm_io_mem   — In-memory reader (wraps a pointer + length)
 */

#include "../../lib/libc/int.h"

/* -------------------------------------------------------------------------
 * Read interface
 * ------------------------------------------------------------------------- */

typedef struct llm_io_read {
    /* Read exactly 'n' bytes into 'dst'.  Returns 0 on success, -1 on error. */
    int  (*read)(struct llm_io_read* io, void* dst, size_t n);
    /* Seek to absolute byte offset.  Returns 0 on success, -1 on error. */
    int  (*seek)(struct llm_io_read* io, uint64_t offset);
    /* Returns current byte position. */
    uint64_t (*tell)(struct llm_io_read* io);
    /* Returns total file size in bytes, or 0 if unknown. */
    uint64_t (*size)(struct llm_io_read* io);
    /* Release resources. */
    void (*close)(struct llm_io_read* io);
    /* Implementation-specific state (opaque to callers). */
    void* priv;
} llm_io_read_t;

/* -------------------------------------------------------------------------
 * Write interface
 * ------------------------------------------------------------------------- */

typedef struct llm_io_write {
    /* Write exactly 'n' bytes from 'src'.  Returns 0 on success, -1 on error. */
    int  (*write)(struct llm_io_write* io, const void* src, size_t n);
    /* Returns current byte position. */
    uint64_t (*tell)(struct llm_io_write* io);
    /* Flush / close.  Returns 0 on success. */
    int  (*close)(struct llm_io_write* io);
    void* priv;
} llm_io_write_t;

/* -------------------------------------------------------------------------
 * Convenience helpers (call through vtable)
 * ------------------------------------------------------------------------- */

static inline int llm_io_read(llm_io_read_t* io, void* dst, size_t n)
    { return io->read(io, dst, n); }
static inline int llm_io_seek(llm_io_read_t* io, uint64_t off)
    { return io->seek(io, off); }
static inline uint64_t llm_io_tell(llm_io_read_t* io)
    { return io->tell(io); }
static inline uint64_t llm_io_size(llm_io_read_t* io)
    { return io->size(io); }
static inline void llm_io_close(llm_io_read_t* io)
    { if (io && io->close) io->close(io); }

static inline int llm_io_write(llm_io_write_t* io, const void* src, size_t n)
    { return io->write(io, src, n); }
static inline int llm_io_write_close(llm_io_write_t* io)
    { return io->close(io); }

/* Typed read helpers */
int llm_io_read_u8 (llm_io_read_t* io, uint8_t*  v);
int llm_io_read_u16(llm_io_read_t* io, uint16_t* v);
int llm_io_read_u32(llm_io_read_t* io, uint32_t* v);
int llm_io_read_u64(llm_io_read_t* io, uint64_t* v);
int llm_io_read_i32(llm_io_read_t* io, int32_t*  v);
int llm_io_read_i64(llm_io_read_t* io, int64_t*  v);
int llm_io_read_f32(llm_io_read_t* io, float*    v);

/* Read a length-prefixed string (uint64 length + bytes).
 * Writes at most (buf_max-1) bytes and NUL-terminates.
 * Returns actual string byte length (before truncation), or -1 on error. */
int64_t llm_io_read_str(llm_io_read_t* io, char* buf, size_t buf_max);

/* -------------------------------------------------------------------------
 * Factory: BranchFS file reader
 *
 * Opens 'path' on the global filesystem instance.
 * Returns NULL on failure.
 * Caller must call llm_io_close() when done.
 * ------------------------------------------------------------------------- */
llm_io_read_t* llm_io_open_file(const char* path);

/* -------------------------------------------------------------------------
 * Factory: In-memory reader
 *
 * Wraps an existing byte buffer.  The buffer must remain valid for the
 * lifetime of the returned reader object to avoid use-after-free.
 * Returns NULL on failure (e.g. kmalloc OOM).
 * ------------------------------------------------------------------------- */
llm_io_read_t* llm_io_open_mem(const void* data, size_t len);

/* -------------------------------------------------------------------------
 * Factory: BranchFS file writer
 *
 * Creates or overwrites 'path'.  Returns NULL on failure.
 * ------------------------------------------------------------------------- */
llm_io_write_t* llm_io_open_write(const char* path);

#endif /* GRACE_LLM_IO_H */
