#ifndef _TEXT_H_
#define _TEXT_H_

#include "../../lib/libc/string.h"
#include "../../lib/libc/int.h"

void text_init(void);
void text_draw(const char* str, int x, int y, uint32_t fg, uint32_t bg, int scale);

#endif /* _TEXT_H_ */