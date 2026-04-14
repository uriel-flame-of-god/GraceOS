// ============================
// GraceOS cfdisk — UI via framebuffer syscalls
// ============================

#ifndef CFDISK_UI_H
#define CFDISK_UI_H

#include "../../../lib/libc/int.h"
#include "cfdisk.h"

/* Draw the full cfdisk interface for the current state */
void ui_draw(cfdisk_state_t* state);

/* Draw a one-line status/error message at the bottom of the screen */
void ui_status(const char* msg);

/* Show a yes/no confirmation prompt.
   Returns 1 if the user pressed 'y', 0 otherwise. */
int ui_confirm(const char* prompt);

#endif /* CFDISK_UI_H */
