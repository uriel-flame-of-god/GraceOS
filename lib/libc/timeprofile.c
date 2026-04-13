/*
 * GraceOS Time Profile Management
 * Handles loading and parsing time profiles from /etc/timeprofiles/
 */

#include "time.h"
#include "string.h"

// External references from time.c
extern int profile_offset;
extern char profile_format[64];
extern int profile_loaded;

// TODO: Replace with actual file I/O syscalls
// For now, we'll use placeholder implementations

/*
 * Parse offset string (e.g., "+05:30" or "-08:00")
 * Returns offset in seconds
 */
static int parse_offset(const char *offset_str) {
    if (!offset_str) {
        return 0;
    }
    
    int sign = 1;
    const char *p = offset_str;
    
    // Parse sign
    if (*p == '+') {
        sign = 1;
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    }
    
    // Parse hours
    int hours = 0;
    while (*p >= '0' && *p <= '9') {
        hours = hours * 10 + (*p - '0');
        p++;
    }
    
    // Skip colon
    if (*p == ':') {
        p++;
    }
    
    // Parse minutes
    int minutes = 0;
    while (*p >= '0' && *p <= '9') {
        minutes = minutes * 10 + (*p - '0');
        p++;
    }
    
    return sign * (hours * 3600 + minutes * 60);
}

/*
 * Parse a single line from profile file
 */
static void parse_profile_line(const char *line, int *offset, char *format) {
    if (!line) {
        return;
    }
    
    // Skip whitespace
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    
    // Check for "offset = "
    if (strncmp(line, "offset", 6) == 0) {
        line += 6;
        while (*line == ' ' || *line == '=') {
            line++;
        }
        *offset = parse_offset(line);
    }
    // Check for "format = "
    else if (strncmp(line, "format", 6) == 0) {
        line += 6;
        while (*line == ' ' || *line == '=') {
            line++;
        }
        
        // Copy format string
        int i = 0;
        while (line[i] && line[i] != '\n' && line[i] != '\r' && i < 63) {
            format[i] = line[i];
            i++;
        }
        format[i] = '\0';
    }
}

/*
 * Load time profile from file
 * 
 * Profile format:
 * name = India
 * offset = +05:30
 * format = DD-MM-YYYY HH:mm:ss A
 */
int load_time_profile(void) {
    if (profile_loaded) {
        return 0;
    }
    
    // TODO: Implement actual file reading
    // For now, use default UTC profile
    
    // Read /etc/timeprofiles/active to get active profile name
    // char active_name[64];
    // int fd = open("/etc/timeprofiles/active", O_RDONLY);
    // if (fd < 0) goto use_default;
    // read(fd, active_name, sizeof(active_name));
    // close(fd);
    
    // Read the actual profile file
    // char path[128];
    // sprintf(path, "/etc/timeprofiles/%s", active_name);
    // fd = open(path, O_RDONLY);
    // if (fd < 0) goto use_default;
    
    // For demonstration, parse a sample profile
    const char *sample_profile = 
        "name = India\n"
        "offset = +05:30\n"
        "format = DD-MM-YYYY HH:mm:ss A\n";
    
    // Parse profile line by line
    int temp_offset = 0;
    char temp_format[64] = "DD-MM-YYYY HH:mm:ss A";
    
    const char *line = sample_profile;
    char line_buf[128];
    int line_idx = 0;
    
    while (*line) {
        if (*line == '\n' || *line == '\r') {
            line_buf[line_idx] = '\0';
            if (line_idx > 0) {
                parse_profile_line(line_buf, &temp_offset, temp_format);
            }
            line_idx = 0;
            line++;
        } else {
            if (line_idx < 127) {
                line_buf[line_idx++] = *line;
            }
            line++;
        }
    }
    
    // Process last line
    if (line_idx > 0) {
        line_buf[line_idx] = '\0';
        parse_profile_line(line_buf, &temp_offset, temp_format);
    }
    
    // Update global profile
    profile_offset = temp_offset;
    strncpy(profile_format, temp_format, sizeof(profile_format) - 1);
    profile_format[63] = '\0';
    
    profile_loaded = 1;
    return 0;

// use_default:
    // Use UTC as default
    profile_offset = 0;
    strncpy(profile_format, "DD-MM-YYYY HH:mm:ss A", sizeof(profile_format) - 1);
    profile_loaded = 1;
    return -1;
}

/*
 * Get current time offset in seconds
 */
int get_time_offset(void) {
    if (!profile_loaded) {
        load_time_profile();
    }
    return profile_offset;
}

/*
 * Get current time format string
 */
const char *get_time_format(void) {
    if (!profile_loaded) {
        load_time_profile();
    }
    return profile_format;
}

/*
 * Reload time profile (for when admin changes settings)
 */
int reload_time_profile(void) {
    profile_loaded = 0;
    return load_time_profile();
}

/*
 * Create a new time profile file
 * (For admin tools)
 */
int create_time_profile(const char *name, const char *offset, const char *format) {
    if (!name || !offset || !format) {
        return -1;
    }
    
    // TODO: Implement file creation
    // char path[128];
    // sprintf(path, "/etc/timeprofiles/%s.gos", name);
    // int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    // if (fd < 0) return -1;
    
    // Write profile data
    // write(fd, "name = ", 7);
    // write(fd, name, strlen(name));
    // write(fd, "\n", 1);
    // write(fd, "offset = ", 9);
    // write(fd, offset, strlen(offset));
    // write(fd, "\n", 1);
    // write(fd, "format = ", 9);
    // write(fd, format, strlen(format));
    // write(fd, "\n", 1);
    
    // close(fd);
    
    return 0;
}

/*
 * Set active time profile
 * (For admin tools)
 */
int set_active_profile(const char *name) {
    if (!name) {
        return -1;
    }
    
    // TODO: Implement file writing
    // int fd = open("/etc/timeprofiles/active", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    // if (fd < 0) return -1;
    
    // write(fd, name, strlen(name));
    // close(fd);
    
    // Reload profile
    return reload_time_profile();
}
