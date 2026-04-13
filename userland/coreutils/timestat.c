/*
 * timestat - Display current time continuously
 * GraceOS Core Utility
 */

#include "../../lib/libc/time.h"
#include "../../lib/libc/string.h"

// TODO: Replace with actual stdio when available
extern int printf(const char *format, ...);
extern void exit(int status);

/*
 * Clear line (for terminal refresh)
 */
static void clear_line(void) {
    printf("\r");
    for (int i = 0; i < 80; i++) {
        printf(" ");
    }
    printf("\r");
}

/*
 * Main entry point
 */
int main(void) {
    char time_buffer[64];
    const char *format;
    
    // Load time profile to get format
    load_time_profile();
    format = get_time_format();
    
    printf("GraceOS Time Monitor\n");
    printf("Press Ctrl+C to exit\n\n");
    
    // Main loop - display time every second
    while (1) {
        // Get current time
        time_t current_time = time(NULL);
        
        // Convert to local time
        struct tm tm;
        localtime_r(&current_time, &tm);
        
        // Format time
        format_time(&tm, format, time_buffer, sizeof(time_buffer));
        
        // Clear line and print
        clear_line();
        printf("%s", time_buffer);
        
        // Flush output (TODO: implement fflush when stdio is ready)
        
        // Sleep for 1 second
        sleep(1);
    }
    
    return 0;
}
