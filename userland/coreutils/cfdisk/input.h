// ============================
// GraceOS cfdisk — Input Layer
// ============================

#ifndef CFDISK_INPUT_H
#define CFDISK_INPUT_H

/* Blocking key read via SYS_GETKEY */
int input_getkey(void);

/* Non-blocking check — returns 0 if no key, 1 if key available */
int input_haskey(void);

/* Special key codes */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_ENTER 0x0D
#define KEY_ESC   0x1B

#endif /* CFDISK_INPUT_H */
