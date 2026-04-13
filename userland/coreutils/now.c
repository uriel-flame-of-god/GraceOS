/*
 * now - Display current date/time
 * GraceOS Core Utility
 */

#include "now.h"
#include "../../lib/libc/time.h"
#include "../../lib/libc/string.h"
#include "../../lib/libgrace/grace.h"

/*
 * Print usage information
 */
static void print_usage(void) {
    print("Usage: now [OPTIONS]\n");
    print("Display current date and time\n\n");
    print("Options:\n");
    print("  --date         Show only date\n");
    print("  --time         Show only time\n");
    print("  --format=1     Use 12-hour format\n");
    print("  --format=2     Use 24-hour format\n");
    print("  --stream       Live updating display (Ctrl+C to stop)\n");
    print("\nExamples:\n");
    print("  now                    Show date and time (24-hour)\n");
    print("  now --date             Show only date\n");
    print("  now --time             Show only time\n");
    print("  now --date --time      Show both date and time\n");
    print("  now --time --format=1  Show time in 12-hour format\n");
    print("  now --stream           Live updating clock\n");
}

/*
 * Parse command line arguments
 */
int now_parse_args(int argc, char **argv, struct now_config *config) {
    if (!config) {
        return -1;
    }
    
    // Default: show date and time in 24-hour format
    config->flags = NOW_SHOW_DATE | NOW_SHOW_TIME | NOW_FORMAT_24H;
    
    int explicit_options = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 1;
        }
        else if (strcmp(argv[i], "--date") == 0) {
            if (!explicit_options) {
                config->flags &= ~(NOW_SHOW_DATE | NOW_SHOW_TIME);
                explicit_options = 1;
            }
            config->flags |= NOW_SHOW_DATE;
        }
        else if (strcmp(argv[i], "--time") == 0) {
            if (!explicit_options) {
                config->flags &= ~(NOW_SHOW_DATE | NOW_SHOW_TIME);
                explicit_options = 1;
            }
            config->flags |= NOW_SHOW_TIME;
        }
        else if (strncmp(argv[i], "--format=", 9) == 0) {
            char format_num = argv[i][9];
            
            // Clear existing format flags
            config->flags &= ~(NOW_FORMAT_12H | NOW_FORMAT_24H);
            
            if (format_num == '1') {
                config->flags |= NOW_FORMAT_12H;
            }
            else if (format_num == '2') {
                config->flags |= NOW_FORMAT_24H;
            }
            else {
                print("Error: Invalid format number. Use 1 for 12-hour or 2 for 24-hour.\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--stream") == 0) {
            config->flags |= NOW_STREAM;
        }
        else {
            print("Error: Unknown option '");
            print(argv[i]);
            print("'\n");
            print_usage();
            return -1;
        }
    }
    
    return 0;
}

/*
 * Format time into buffer (without newline)
 */
static void format_time_buffer(struct now_config *config, char *buffer, size_t bufsize) {
    // Get current time
    time_t current_time = time(NULL);
    
    buffer[0] = '\0';
    
    if (current_time == -1) {
        strcat(buffer, "Error");
        return;
    }
    
    // Convert to local time
    struct tm tm;
    if (!localtime_r(&current_time, &tm)) {
        strcat(buffer, "Error");
        return;
    }
    
    // Display date if requested
    if (config->flags & NOW_SHOW_DATE) {
        char date_buf[32];
        format_time(&tm, "DD-MM-YYYY", date_buf, sizeof(date_buf));
        strcat(buffer, date_buf);
    }
    
    // Add separator if showing both
    if ((config->flags & NOW_SHOW_DATE) && (config->flags & NOW_SHOW_TIME)) {
        strcat(buffer, " ");
    }
    
    // Display time if requested
    if (config->flags & NOW_SHOW_TIME) {
        char time_buf[32];
        
        if (config->flags & NOW_FORMAT_12H) {
            format_time(&tm, "hh:mm:ss A", time_buf, sizeof(time_buf));
        } else {
            format_time(&tm, "HH:mm:ss", time_buf, sizeof(time_buf));
        }
        
        strcat(buffer, time_buf);
    }
    
    (void)bufsize;  // Unused for now
}

/*
 * Display current date/time
 */
int now_display(struct now_config *config) {
    if (!config) {
        return -1;
    }
    
    char buffer[128];
    format_time_buffer(config, buffer, sizeof(buffer));
    
    println(buffer);
    return 0;
}

/*
 * Stream display - continuously update time until Ctrl+C
 */
int now_stream(struct now_config *config) {
    if (!config) {
        return -1;
    }
    
    char buffer[128];
    char prev_buffer[128];
    prev_buffer[0] = '\0';
    
    print("Streaming time (Ctrl+C to stop)...\n");
    
    while (1) {
        // Check for Ctrl+C (ASCII 0x03) - non-blocking
        if (haskey()) {
            int key = getkey();
            if (key == 0x03) {  // Ctrl+C
                print("\n");
                break;
            }
        }
        
        // Format current time
        format_time_buffer(config, buffer, sizeof(buffer));
        
        // Only update display if time changed
        if (strcmp(buffer, prev_buffer) != 0) {
            // Carriage return to overwrite line
            print("\r");
            print(buffer);
            print("   ");  // Clear any trailing characters
            
            // Save current buffer
            strcpy(prev_buffer, buffer);
        }
    }
    
    return 0;
}

/*
 * Main entry point
 */
int main(int argc, char **argv) {
    struct now_config config;
    
    // Parse command line arguments
    int result = now_parse_args(argc, argv, &config);
    if (result != 0) {
        return (result < 0) ? 1 : 0;
    }
    
    // Check for stream mode
    if (config.flags & NOW_STREAM) {
        return now_stream(&config);
    }
    
    // Display current date/time (one-shot)
    if (now_display(&config) != 0) {
        return 1;
    }
    
    return 0;
}
