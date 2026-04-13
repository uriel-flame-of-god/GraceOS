#ifndef GRACEOS_SYS_TYPES_H
#define GRACEOS_SYS_TYPES_H

/* Include the GraceOS libc types (already has ssize_t, uintptr_t, etc.) */
#include <int.h>

/* Define only POSIX types not in libc */
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef unsigned int mode_t;
typedef long off_t;
typedef unsigned long ino_t;

#endif
