#include "../libc/int.h"
#include "stdio.h"

static int put_char(char* buf, size_t max, size_t pos, char c)
{
    if (pos + 1 < max) {
        buf[pos] = c;
    }
    return 1;
}

static int put_str(char* buf, size_t max, size_t pos, const char* s)
{
    int written = 0;
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        written += put_char(buf, max, pos + (size_t)written, *s++);
    }
    return written;
}

static int put_uint64(char* buf, size_t max, size_t pos, uint64_t v, int base, int upper)
{
    char tmp[24];
    int n = 0;
    int i;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v) {
        tmp[n++] = digits[v % (uint64_t)base];
        v /= (uint64_t)base;
    }

    int written = 0;
    for (i = n - 1; i >= 0; i--) {
        written += put_char(buf, max, pos + (size_t)written, tmp[i]);
    }
    return written;
}

static int put_int64(char* buf, size_t max, size_t pos, int64_t v)
{
    int written = 0;
    if (v < 0) {
        written += put_char(buf, max, pos, '-');
        v = -v;
    }
    written += put_uint64(buf, max, pos + (size_t)written, (uint64_t)v, 10, 0);
    return written;
}

static int put_float(char* buf, size_t max, size_t pos, float f)
{
    int written = 0;
    int i;
    int digit;
    if (f < 0.0f) {
        written += put_char(buf, max, pos, '-');
        f = -f;
    }

    uint64_t ipart = (uint64_t)f;
    float fpart = f - (float)ipart;

    written += put_uint64(buf, max, pos + (size_t)written, ipart, 10, 0);
    written += put_char(buf, max, pos + (size_t)written, '.');

    for (i = 0; i < 4; i++) {
        fpart *= 10.0f;
        digit = (int)fpart;
        written += put_char(buf, max, pos + (size_t)written, (char)('0' + digit));
        fpart -= (float)digit;
    }

    return written;
}

int vsnprintf(char* str, size_t size, const char* format, va_list ap)
{
    size_t pos = 0;
    int written = 0;
    int is_long;

    while (*format) {
        if (*format != '%') {
            written += put_char(str, size, pos + (size_t)written, *format++);
            continue;
        }

        format++;
        is_long = 0;
        if (*format == 'l') { is_long++; format++; }
        if (*format == 'l') { is_long++; format++; }

        switch (*format) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            written += put_str(str, size, pos + (size_t)written, s);
            break;
        }
        case 'd': {
            int64_t v = (is_long >= 2) ? va_arg(ap, long long)
                      : (is_long == 1) ? va_arg(ap, long)
                      :                  va_arg(ap, int);
            written += put_int64(str, size, pos + (size_t)written, v);
            break;
        }
        case 'u': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, unsigned long long)
                       : (is_long == 1) ? va_arg(ap, unsigned long)
                       :                  va_arg(ap, unsigned int);
            written += put_uint64(str, size, pos + (size_t)written, v, 10, 0);
            break;
        }
        case 'x': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, unsigned long long)
                       : (is_long == 1) ? va_arg(ap, unsigned long)
                       :                  va_arg(ap, unsigned int);
            written += put_uint64(str, size, pos + (size_t)written, v, 16, 0);
            break;
        }
        case 'X': {
            uint64_t v = (is_long >= 2) ? va_arg(ap, unsigned long long)
                       : (is_long == 1) ? va_arg(ap, unsigned long)
                       :                  va_arg(ap, unsigned int);
            written += put_uint64(str, size, pos + (size_t)written, v, 16, 1);
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            written += put_char(str, size, pos + (size_t)written, (char)c);
            break;
        }
        case 'f': {
            float f = (float)va_arg(ap, double);
            written += put_float(str, size, pos + (size_t)written, f);
            break;
        }
        case '%':
            written += put_char(str, size, pos + (size_t)written, '%');
            break;
        default:
            written += put_char(str, size, pos + (size_t)written, '?');
            break;
        }

        format++;
    }

    if (size > 0) {
        size_t end = pos + (size_t)written;
        str[end < size ? end : size - 1] = '\0';
    }

    return written;
}

int vsprintf(char* str, const char* format, va_list ap)
{
    return vsnprintf(str, (size_t)-1, format, ap);
}

int snprintf(char* str, size_t size, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int written = vsnprintf(str, size, format, ap);
    va_end(ap);
    return written;
}

int sprintf(char* str, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int written = vsprintf(str, format, ap);
    va_end(ap);
    return written;
}
