#include <stdlib.h>

__attribute__((noreturn)) void
abort(void)
{
    __builtin_trap();
    for (;;) {
    }
}
