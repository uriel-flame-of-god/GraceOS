// ============================
// GraceOS Shell History
// Command history management
// ============================

#ifndef GRACEOS_SHELL_HISTORY_H
#define GRACEOS_SHELL_HISTORY_H

#define HIST_MAX 32
#define HIST_LEN 256

/* Initialize history system */
void hist_init(void);

/* Add command to history */
void hist_add(const char* cmd);

/* Get previous command in history */
const char* hist_prev(void);

/* Get next command in history */
const char* hist_next(void);

/* Get command by index (0 = oldest) */
const char* hist_get(int index);

/* Get current history count */
int hist_count(void);

/* Reset browsing position */
void hist_reset_pos(void);

#endif /* GRACEOS_SHELL_HISTORY_H */
