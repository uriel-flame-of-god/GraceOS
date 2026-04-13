# GraceOS Time System

This document describes how time works in GraceOS, from the hardware clock through kernel timekeeping to userland time functions.

## Overview

GraceOS tracks time using the Unix epoch (seconds since January 1, 1970 00:00:00 UTC). At boot, the kernel reads the hardware real-time clock to get the current time. A programmable timer then fires 1000 times per second to keep the system clock advancing. Userland programs access time through syscalls and the libc time API.

## Hardware Layer

### Real-Time Clock (RTC)

The RTC is a battery-backed clock that keeps time even when the computer is off. GraceOS reads it once at boot to initialize the system time. The RTC stores values in BCD format (binary-coded decimal), so the driver converts these to normal integers.

Files:
- drivers/storage/rtc.h
- drivers/storage/rtc.c

The rtc_init() function reads the year, month, day, hour, minute, and second from CMOS ports 0x70 and 0x71, then calls rtc_init_time() to convert this to Unix time.

### Programmable Interval Timer (PIT)

The PIT generates interrupts at a configurable rate. GraceOS sets it to 1000 Hz, meaning an interrupt fires every millisecond. Each interrupt calls timer_tick() which increments the system time by one millisecond.

The PIT is configured during IDT initialization in kernel/arch/x86_64/idt.c. The base frequency is 1193182 Hz, divided down to 1000 Hz.

## Kernel Layer

### Time Structures

The kernel uses two clocks:

system_time is the real-world time in UTC. It can be adjusted if needed (for example, by an administrator setting the clock). This is what you get from time() and clock_gettime(CLOCK_REALTIME).

monotonic_time always moves forward and is never adjusted. It starts at the same value as system_time at boot but then ticks independently. This is useful for measuring elapsed time because it will not jump around if someone changes the system clock. You get this from clock_gettime(CLOCK_MONOTONIC).

Both clocks use struct timespec which holds seconds and nanoseconds. Although the PIT only gives millisecond precision, the structure can represent nanoseconds for future improvements.

Files:
- kernel/include/time.h
- kernel/core/time.c

### Timer Tick

Every millisecond, the PIT fires IRQ0 which calls timer_tick(). This function adds one million nanoseconds (one millisecond) to both system_time and monotonic_time. When nanoseconds reach one billion, it rolls over and increments seconds.

### Syscalls

Four syscalls provide time access:

SYS_TIME (25) returns the current time as seconds since the epoch. If you pass a pointer, it also stores the value there. This matches the POSIX time() function.

SYS_CLOCK_GETTIME (26) fills a timespec structure with the current time. Pass CLOCK_REALTIME for system time or CLOCK_MONOTONIC for the monotonic clock.

SYS_GETTIMEOFDAY (27) is a legacy interface that fills a timeval structure (seconds and microseconds). Provided for compatibility.

SYS_SETTIMEOFDAY (28) sets the system time. This requires root privileges.

Files:
- kernel/sys/sys_time.c

## Userland Layer

### libc Time API

The C library provides POSIX-compatible time functions that wrap the kernel syscalls.

time(t) returns seconds since the epoch and optionally stores it in the pointer t.

clock_gettime(clk, ts) gets high-resolution time into a timespec structure.

gmtime_r(time, tm) converts Unix time to a broken-down calendar representation in UTC.

localtime_r(time, tm) converts Unix time to local time by applying the time profile offset.

mktime(tm) converts a broken-down time back to Unix seconds.

strftime(buf, size, fmt, tm) formats time according to a POSIX format string.

format_time(tm, fmt, buf, size) formats time using GraceOS format tokens.

Files:
- lib/libc/time.h
- lib/libc/time.c
- lib/libc/time_syscall.c

### Format Tokens

The format_time() function supports these tokens:

| Token | Meaning |
|-------|---------|
| DD | Day of month, zero-padded (01-31) |
| MM | Month, zero-padded (01-12) |
| YYYY | Four-digit year |
| HH | Hour in 24-hour format (00-23) |
| hh | Hour in 12-hour format (01-12) |
| mm | Minutes (00-59) |
| ss | Seconds (00-59) |
| A | AM or PM |

Example: format_time(tm, "DD-MM-YYYY HH:mm:ss", buf, sizeof(buf)) produces "31-01-2026 14:30:45".

### Time Profiles

A time profile specifies an offset from UTC and a preferred display format. Profiles let users see times in their local timezone.

Profile format:
```
name = Profile Name
offset = +05:30
format = DD-MM-YYYY HH:mm:ss A
```

The offset uses +HH:MM or -HH:MM notation. The library parses this into seconds (for example, +05:30 becomes 19800 seconds).

Files:
- lib/libc/timeprofile.c

## Boot Sequence

1. Kernel starts
2. kmain() calls rtc_init() which reads the hardware clock
3. rtc_init() converts the RTC values to Unix time and stores in system_time
4. idt_init() configures the PIT to fire at 1000 Hz and unmasks IRQ0
5. Interrupts are enabled with sti
6. Every millisecond, timer_tick() increments the clocks
7. Shell starts
8. User runs now command which calls time() syscall

## Accuracy

The system clock has millisecond resolution from the PIT. The RTC typically has one-second resolution. There is no network time synchronization yet, so the clock may drift over time. The monotonic clock is useful when you need reliable elapsed time measurements regardless of clock adjustments.

## Future Work

- Sleep syscalls that integrate with the scheduler
- Kernel timers for delayed execution
- Alarm signals for processes
- Writing back to the RTC on shutdown
- NTP client for network time synchronization
- Daylight saving time rules
- High precision timing using HPET or TSC
