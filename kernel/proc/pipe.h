// ============================
// GraceOS Kernel Pipe
// Unidirectional byte-stream IPC
// ============================

#ifndef GRACEOS_PIPE_H
#define GRACEOS_PIPE_H

#include "../../lib/libc/int.h"

/* ============================
   Pipe Configuration
   ============================ */

#define PIPE_TABLE_SIZE  64          /* Max simultaneous pipes */
#define PIPE_BUFFER_SIZE 4096        /* Ring buffer per pipe   */
#define PIPE_FD_BASE     3           /* First fd index for pipes/files */

/* ============================
   Pipe Structure
   ============================ */

typedef struct pipe {
    uint8_t  buf[PIPE_BUFFER_SIZE];  /* Ring buffer */
    uint32_t read_pos;               /* Next read index  */
    uint32_t write_pos;              /* Next write index */
    uint32_t count;                  /* Bytes available  */
    int      readers;                /* Open read ends   */
    int      writers;                /* Open write ends  */
    int      valid;                  /* Slot in use      */
} pipe_t;

/* ============================
   Pipe API
   ============================ */

/* Initialize the global pipe table */
void pipe_init(void);

/* Allocate a new pipe; returns pipe id (>= 0) or -1 on error */
int pipe_alloc(void);

/* Read up to len bytes from pipe; returns bytes read, 0 = EOF, -1 = error */
int pipe_read(int pipe_id, void* buf, int len);

/* Write len bytes to pipe; returns bytes written or -1 on error */
int pipe_write(int pipe_id, const void* buf, int len);

/* Close the read end of a pipe */
void pipe_close_read(int pipe_id);

/* Close the write end of a pipe */
void pipe_close_write(int pipe_id);

/* Get a pipe by id (NULL if invalid) */
pipe_t* pipe_get(int pipe_id);

#endif /* GRACEOS_PIPE_H */
