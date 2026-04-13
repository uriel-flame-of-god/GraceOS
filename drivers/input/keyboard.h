#ifndef GRACEOS_KEYBOARD_H
#define GRACEOS_KEYBOARD_H

#include "../../lib/libc/int.h"

/* ============================
   PS/2 Keyboard Driver
   ============================ */

/* Special Keys */
#define KEY_ESCAPE      0x01
#define KEY_BACKSPACE   0x0E
#define KEY_TAB         0x0F
#define KEY_ENTER       0x1C
#define KEY_LCTRL       0x1D
#define KEY_LSHIFT      0x2A
#define KEY_RSHIFT      0x36
#define KEY_LALT        0x38
#define KEY_CAPSLOCK    0x3A
#define KEY_F1          0x3B
#define KEY_F2          0x3C
#define KEY_F3          0x3D
#define KEY_F4          0x3E
#define KEY_F5          0x3F
#define KEY_F6          0x40
#define KEY_F7          0x41
#define KEY_F8          0x42
#define KEY_F9          0x43
#define KEY_F10         0x44
#define KEY_NUMLOCK     0x45
#define KEY_SCROLLLOCK  0x46

/* Arrow Keys (Extended - E0 prefix) */
#define KEY_UP          0x48
#define KEY_DOWN        0x50
#define KEY_LEFT        0x4B
#define KEY_RIGHT       0x4D

/* Navigation Keys (Extended - E0 prefix) */
#define KEY_HOME        0x47
#define KEY_END         0x4F
#define KEY_PGUP        0x49
#define KEY_PGDN        0x51
#define KEY_INS         0x52
#define KEY_DEL         0x53
#define KEY_RCTRL       0x1D  /* Right Ctrl (same scancode as LCTRL but with E0) */
#define KEY_RALT        0x38  /* Right Alt  (same scancode as LALT  but with E0) */

/* Special return values for extended keys */
#define KEY_SPECIAL_UP      0x80
#define KEY_SPECIAL_DOWN    0x81
#define KEY_SPECIAL_LEFT    0x82
#define KEY_SPECIAL_RIGHT   0x83
#define KEY_SPECIAL_HOME    0x84
#define KEY_SPECIAL_END     0x85
#define KEY_SPECIAL_PGUP    0x86
#define KEY_SPECIAL_PGDN    0x87
#define KEY_SPECIAL_INS     0x88
#define KEY_SPECIAL_DEL     0x89

/* ASCII Control Codes */
#define KEY_CTRL_S      0x13  /* DC3 - Save */
#define KEY_CTRL_X      0x18  /* CAN - Exit */


#define KEY_SPACE        0x39

/* API Functions */
void keyboard_init(void);
void keyboard_handler(void);
unsigned char keyboard_getchar(void);
unsigned char keyboard_getchar_nonblocking(void);
int keyboard_haschar();
void keyboard_service_run(void);

#endif /* GRACEOS_KEYBOARD_H */
