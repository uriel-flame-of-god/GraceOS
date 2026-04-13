/*
 * now.h - Display current date/time utility header
 */

#ifndef NOW_H
#define NOW_H

/*
 * Display flags
 */
#define NOW_SHOW_DATE   0x01
#define NOW_SHOW_TIME   0x02
#define NOW_FORMAT_12H  0x04
#define NOW_FORMAT_24H  0x08
#define NOW_STREAM      0x10

/*
 * Configuration
 */
struct now_config {
    int flags;
};

/*
 * Parse command line arguments
 */
int now_parse_args(int argc, char **argv, struct now_config *config);

/*
 * Display current date/time (one-shot)
 */
int now_display(struct now_config *config);

/*
 * Stream display - continuously update until Ctrl+C
 */
int now_stream(struct now_config *config);

#endif // NOW_H
