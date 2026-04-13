/*
 * timestat.h - Time monitoring utility header
 */

#ifndef TIMESTAT_H
#define TIMESTAT_H

/*
 * Display modes
 */
#define TIMESTAT_MODE_CONTINUOUS  0  // Continuous update
#define TIMESTAT_MODE_ONCE        1  // Display once and exit

/*
 * Configuration
 */
struct timestat_config {
    int mode;
    int refresh_rate;  // Seconds between updates
    const char *format;  // Custom format (NULL = use profile)
};

/*
 * Display current time
 */
void timestat_display(const char *format);

/*
 * Run time monitor
 */
int timestat_run(struct timestat_config *config);

#endif // TIMESTAT_H
