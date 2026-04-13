# now Command

The now command displays the current date and time in GraceOS.

## Usage

```
now [OPTIONS]
```

## Options

| Option | Description |
|--------|-------------|
| --date | Show only the date |
| --time | Show only the time |
| --format=1 | Use 12-hour format with AM/PM |
| --format=2 | Use 24-hour format (this is the default) |
| --stream | Continuously update the display until Ctrl+C |
| --help | Show help message |

## Examples

Show date and time using the default 24-hour format:
```
now
```
Output: `31-01-2026 14:30:45`

Show only the date:
```
now --date
```
Output: `31-01-2026`

Show only the time:
```
now --time
```
Output: `14:30:45`

Show both date and time explicitly:
```
now --date --time
```
Output: `31-01-2026 14:30:45`

Show time in 12-hour format:
```
now --time --format=1
```
Output: `02:30:45 PM`

Show everything in 12-hour format:
```
now --date --time --format=1
```
Output: `31-01-2026 02:30:45 PM`

Live updating clock that refreshes every second:
```
now --stream
```
Press Ctrl+C to stop the stream and return to the shell.

Combine streaming with other options:
```
now --stream --time --format=1
```
This shows a live 12-hour clock.

## Date Format

Dates are displayed as DD-MM-YYYY where DD is the day (01-31), MM is the month (01-12), and YYYY is the four-digit year.

## Time Formats

The 24-hour format shows HH:mm:ss where HH ranges from 00 to 23.

The 12-hour format shows hh:mm:ss A where hh ranges from 01 to 12 and A is either AM or PM.

## Streaming Mode

When you use --stream, the command enters a loop that updates the time display on the same line. The time refreshes whenever the second changes. Press Ctrl+C to exit streaming mode and return to the shell prompt.

Streaming mode works with all other options, so you can stream just the time, just the date, or use 12-hour format while streaming.

## Time Source

The time comes from the kernel's system clock, which is initialized from the hardware RTC at boot and kept current by the PIT timer running at 1000 Hz. The displayed time uses the active time profile's offset, so it reflects local time rather than UTC.

## Files

Source code: userland/coreutils/now.c
Header: userland/coreutils/now.h
