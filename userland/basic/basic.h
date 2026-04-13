#ifndef GRACEOS_BASIC_H
#define GRACEOS_BASIC_H

#include "../../lib/libc/int.h"

/* GraceBASIC Interpreter */

#define B_MAX_LINES 1024
#define B_MAX_LINE_LEN 128
#define B_MAX_VARS 26

/* Program line */
struct basic_line {
    int number;
    char text[B_MAX_LINE_LEN];
};

/* Program storage */
struct basic_program {
    struct basic_line lines[B_MAX_LINES];
    int count;
};

/* Variable (A-Z) */
struct basic_var {
    char name;
    int value;
};

/* Interpreter state */
struct basic_state {
    struct basic_program program;
    struct basic_var vars[B_MAX_VARS];
    int pc;  // Program counter
    int running;
};

/* Main entry point */
void basic_run(void);

#endif /* GRACEOS_BASIC_H */
