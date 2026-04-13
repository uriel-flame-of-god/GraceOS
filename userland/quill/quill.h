#ifndef GRACEOS_QUILL_H
#define GRACEOS_QUILL_H

#include "../../lib/libc/int.h"

/* Quill Text Editor */

#define Q_MAX_LINES 2048
#define Q_MAX_COLS  256

struct quill_buffer {
    char lines[Q_MAX_LINES][Q_MAX_COLS];
    int line_count;
    int modified;
    char filename[128];
};

struct quill_cursor {
    int x;  // column
    int y;  // row (line number)
};

/* Editor functions */
void quill_run(const char* filename);

#endif /* GRACEOS_QUILL_H */
