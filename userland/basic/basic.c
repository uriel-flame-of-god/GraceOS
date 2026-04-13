// GraceBASIC Interpreter for GraceOS
// QBASIC-compatible BASIC interpreter

#include "basic.h"
#include "../../lib/libc/string.h"
#include "../../drivers/video/tty.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/storage/bfs.h"

extern struct bfs_instance g_bfs;

static struct basic_state state;
static char input_buffer[256];

/* Forward declarations */
static void execute_line(const char* line);
static void cmd_run(void);

/* Helper: Print integer */
static void print_int(int value)
{
    if (value < 0) {
        tty_putchar('-');
        value = -value;
    }
    
    if (value == 0) {
        tty_putchar('0');
        return;
    }
    
    char buf[20];
    int i = 0;
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while (i > 0) {
        tty_putchar(buf[--i]);
    }
}

/* Helper: Parse integer from string */
static int parse_int(const char* str, int* out)
{
    int value = 0;
    int negative = 0;
    
    if (*str == '-') {
        negative = 1;
        str++;
    }
    
    if (*str < '0' || *str > '9') return 0;
    
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        str++;
    }
    
    *out = negative ? -value : value;
    return 1;
}

/* Helper: Skip whitespace */
static const char* skip_whitespace(const char* str)
{
    while (*str == ' ' || *str == '\t') str++;
    return str;
}

/* Helper: Get variable */
static int* get_var(char name)
{
    if (name >= 'A' && name <= 'Z') {
        return &state.vars[name - 'A'].value;
    } else if (name >= 'a' && name <= 'z') {
        return &state.vars[name - 'a'].value;
    }
    return NULL;
}

/* Helper: Evaluate simple expression */
static int eval_expr(const char* expr)
{
    expr = skip_whitespace(expr);
    
    // Variable?
    if ((*expr >= 'A' && *expr <= 'Z') || (*expr >= 'a' && *expr <= 'z')) {
        int* var = get_var(*expr);
        if (var) return *var;
        return 0;
    }
    
    // Number?
    int value;
    if (parse_int(expr, &value)) {
        return value;
    }
    
    // TODO: Support operators (+, -, *, /)
    return 0;
}

/* Command: PRINT */
static void cmd_print(const char* args)
{
    args = skip_whitespace(args);
    
    // String literal?
    if (*args == '"') {
        args++;
        while (*args && *args != '"') {
            tty_putchar(*args);
            args++;
        }
        tty_print("\n");
    } else {
        // Expression
        int value = eval_expr(args);
        print_int(value);
        tty_print("\n");
    }
}

/* Command: LET */
static void cmd_let(const char* args)
{
    args = skip_whitespace(args);
    
    // Get variable name
    if ((*args < 'A' || *args > 'Z') && (*args < 'a' || *args > 'z')) {
        log_error("Invalid variable name");
        return;
    }
    
    char var_name = *args;
    args++;
    
    // Expect '='
    args = skip_whitespace(args);
    if (*args != '=') {
        log_error("Expected '=' in LET statement");
        return;
    }
    args++;
    
    // Evaluate expression
    int value = eval_expr(args);
    
    // Set variable
    int* var = get_var(var_name);
    if (var) {
        *var = value;
    }
}

/* Command: INPUT */
static void cmd_input(const char* args)
{
    args = skip_whitespace(args);
    
    // Get variable name
    if ((*args < 'A' || *args > 'Z') && (*args < 'a' || *args > 'z')) {
        log_error("Invalid variable name");
        return;
    }
    
    char var_name = *args;
    
    // Prompt
    tty_print("? ");
    
    // Read input
    char buffer[32];
    int i = 0;
    while (i < 31) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') break;
        if (c == '\b' && i > 0) {
            i--;
            tty_print("\b \b");
        } else if (c >= '0' && c <= '9') {
            buffer[i++] = c;
            tty_putchar(c);
        } else if (c == '-' && i == 0) {
            buffer[i++] = c;
            tty_putchar(c);
        }
    }
    buffer[i] = '\0';
    tty_print("\n");
    
    // Parse and store
    int value;
    if (parse_int(buffer, &value)) {
        int* var = get_var(var_name);
        if (var) {
            *var = value;
        }
    }
}

/* Command: GOTO */
static void cmd_goto(const char* args)
{
    int line_num;
    if (!parse_int(args, &line_num)) {
        log_error("Invalid line number");
        return;
    }
    
    // Find line
    for (int i = 0; i < state.program.count; i++) {
        if (state.program.lines[i].number == line_num) {
            state.pc = i;
            return;
        }
    }
    
    log_error("Undefined line number");
}

/* Command: IF */
static void cmd_if(const char* args)
{
    // Simple IF: IF A>5 THEN GOTO 100
    // TODO: Implement proper comparison parser
    
    // For now, just skip
    log_warn("IF not fully implemented yet");
}

/* Command: LIST */
static void cmd_list(void)
{
    for (int i = 0; i < state.program.count; i++) {
        print_int(state.program.lines[i].number);
        tty_print(" ");
        tty_print(state.program.lines[i].text);
        tty_print("\n");
    }
}

/* Command: RUN */
static void cmd_run(void)
{
    state.pc = 0;
    state.running = 1;
    
    while (state.running && state.pc < state.program.count) {
        const char* line = state.program.lines[state.pc].text;
        
        // Execute line
        execute_line(line);
        
        state.pc++;
    }
    
    state.running = 0;
}

/* Command: NEW */
static void cmd_new(void)
{
    memset(&state.program, 0, sizeof(state.program));
    log_success("Program cleared");
}

/* Command: END */
static void cmd_end(void)
{
    state.running = 0;
}

/* Execute a line */
static void execute_line(const char* line)
{
    line = skip_whitespace(line);
    
    // Parse command
    if (strncmp(line, "PRINT", 5) == 0) {
        cmd_print(line + 5);
    }
    else if (strncmp(line, "LET", 3) == 0) {
        cmd_let(line + 3);
    }
    else if (strncmp(line, "INPUT", 5) == 0) {
        cmd_input(line + 5);
    }
    else if (strncmp(line, "GOTO", 4) == 0) {
        cmd_goto(line + 4);
    }
    else if (strncmp(line, "IF", 2) == 0) {
        cmd_if(line + 2);
    }
    else if (strncmp(line, "END", 3) == 0) {
        cmd_end();
    }
    else if (strncmp(line, "REM", 3) == 0) {
        // Comment - do nothing
    }
    else if (*line != '\0') {
        log_error("Unknown command");
    }
}

/* Add program line */
static void add_program_line(int number, const char* text)
{
    // Find insertion point
    int pos = 0;
    while (pos < state.program.count && state.program.lines[pos].number < number) {
        pos++;
    }
    
    // Replace existing line?
    if (pos < state.program.count && state.program.lines[pos].number == number) {
        if (*text == '\0') {
            // Delete line
            for (int i = pos; i < state.program.count - 1; i++) {
                state.program.lines[i] = state.program.lines[i + 1];
            }
            state.program.count--;
        } else {
            // Replace line
            strncpy(state.program.lines[pos].text, text, B_MAX_LINE_LEN - 1);
        }
        return;
    }
    
    // Insert new line
    if (state.program.count >= B_MAX_LINES) {
        log_error("Program too large");
        return;
    }
    
    // Shift lines
    for (int i = state.program.count; i > pos; i--) {
        state.program.lines[i] = state.program.lines[i - 1];
    }
    
    state.program.lines[pos].number = number;
    strncpy(state.program.lines[pos].text, text, B_MAX_LINE_LEN - 1);
    state.program.count++;
}

/* Read input line */
static void read_input(char* buffer, int max_len)
{
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_getchar();
        
        if (c == '\n' || c == '\r') {
            buffer[i] = '\0';
            tty_print("\n");
            return;
        }
        
        if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
                tty_print("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            buffer[i++] = c;
            tty_putchar(c);
        }
    }
    buffer[max_len - 1] = '\0';
}

/* Main BASIC REPL */
void basic_run(void)
{
    log_init("Starting GraceBASIC");
    tty_print("GraceBASIC v1.0\n");
    tty_print("Ready.\n\n");
    
    // Initialize state
    memset(&state, 0, sizeof(state));
    
    // REPL
    while (1) {
        tty_print("Ok\n");
        tty_print("> ");
        
        read_input(input_buffer, sizeof(input_buffer));
        
        // Empty line?
        if (input_buffer[0] == '\0') continue;
        
        // Exit?
        if (strcmp(input_buffer, "EXIT") == 0 || strcmp(input_buffer, "QUIT") == 0) {
            break;
        }
        
        // Check if line starts with number (program mode)
        int line_num;
        const char* ptr = input_buffer;
        
        if (*ptr >= '0' && *ptr <= '9') {
            // Parse line number
            if (parse_int(ptr, &line_num)) {
                // Skip number
                while (*ptr >= '0' && *ptr <= '9') ptr++;
                ptr = skip_whitespace(ptr);
                
                // Add to program
                add_program_line(line_num, ptr);
                continue;
            }
        }
        
        // Immediate mode - execute commands
        if (strncmp(input_buffer, "LIST", 4) == 0) {
            cmd_list();
        }
        else if (strncmp(input_buffer, "RUN", 3) == 0) {
            cmd_run();
        }
        else if (strncmp(input_buffer, "NEW", 3) == 0) {
            cmd_new();
        }
        else {
            // Execute as statement
            execute_line(input_buffer);
        }
    }
    
    log_success("GraceBASIC closed");
}
