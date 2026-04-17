#include "shell.h"
#include "cmd_sysinfo.h"
#include "history.h"
#include "../coreutils/now.h"
#include "../quill/quill.h"
#include "../chivm/chivm.h"
#include "../7z/7c.h"
#include "../gui/gui.h"
#include "../mplayer/mplayer.h"
#include "../../kernel/include/syscall.h"
#include "../../drivers/video/tty.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/storage/bfs.h"
#include "../../kernel/mm/pmm/pmm.h"
#include "../../kernel/mm/vmm/vmm.h"
#include "../../kernel/mm/sasy/sasy.h"
#include "../../kernel/log/klog.h"
#include "../../lib/libc/string.h"
#include "../../kernel/sys/power.h"
#include "../../kernel/include/time.h"
#include "../../kernel/proc/proc.h"
#include "../snake/snake.h"
#include "../../kernel/net/net.h"
#include "../../kernel/net/icmp/icmp.h"
#include "../../kernel/net/http/http_client.h"

#define CMD_BUFFER_SIZE 256
#define SHELL_PATH_MAX 64
#define SHELL_MAX_SEGMENTS 16
#define SHELL_SEGMENT_MAX 24
#define SHELL_MAX_ARGS 16

/* Command buffer */
static char cmd_buffer[CMD_BUFFER_SIZE];
static char shell_cwd[SHELL_PATH_MAX] = "/";

/* Forward declarations */
static void shell_print_dec(uint64_t value);

/* External BFS instance */
extern struct bfs_instance g_bfs;

/* ============================
   Helper Functions
   ============================ */

static int strstart(const char* str, const char* prefix)
{
    while (*prefix)
    {
        if (*str != *prefix)
            return 0;
        str++;
        prefix++;
    }
    return 1;
}

static const char* get_args(const char* cmd)
{
    while (*cmd && *cmd != ' ')
        cmd++;
    while (*cmd == ' ')
        cmd++;
    return cmd;
}

static int shell_copy_str(char* dst, const char* src, int dst_size)
{
    if (!dst || !src || dst_size <= 0)
        return 0;

    int i = 0;
    for (; i < dst_size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return src[i] == '\0';
}

static int shell_tokenize_args(const char* args, char* buf, int buf_len, char** argv, int max_args)
{
    if (!args || !buf || buf_len <= 0 || !argv || max_args <= 0)
        return -1;

    if (!shell_copy_str(buf, args, buf_len))
        return -1;

    int argc = 0;
    char* p = buf;
    while (*p)
    {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        if (argc >= max_args)
            return -1;
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }

    return argc;
}

/* Normalize an input path against the current working directory. */
static int shell_resolve_path(const char* input, char* out, int out_len)
{
    if (!input || !out || out_len < 2)
        return 0;

    char segments[SHELL_MAX_SEGMENTS][SHELL_SEGMENT_MAX];
    int seg_count = 0;

    /* Seed with cwd segments for relative paths. */
    if (input[0] != '/')
    {
        const char* c = shell_cwd;
        while (*c)
        {
            while (*c == '/')
                c++;
            if (!*c)
                break;

            int seg_len = 0;
            while (*c && *c != '/')
            {
                if (seg_len + 1 >= SHELL_SEGMENT_MAX)
                    return 0;
                segments[seg_count][seg_len++] = *c++;
            }

            if (seg_len > 0)
            {
                if (seg_count >= SHELL_MAX_SEGMENTS)
                    return 0;
                segments[seg_count][seg_len] = '\0';
                seg_count++;
            }
        }
    }

    /* Apply input segments. */
    const char* p = input;
    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        const char* seg_start = p;
        int seg_len = 0;
        while (*p && *p != '/')
        {
            if (seg_len + 1 >= SHELL_SEGMENT_MAX)
                return 0;
            seg_len++;
            p++;
        }

        if (seg_len == 1 && seg_start[0] == '.')
            continue;
        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.')
        {
            if (seg_count > 0)
                seg_count--;
            continue;
        }

        if (seg_count >= SHELL_MAX_SEGMENTS)
            return 0;

        for (int i = 0; i < seg_len; i++)
            segments[seg_count][i] = seg_start[i];
        segments[seg_count][seg_len] = '\0';
        seg_count++;
    }

    int pos = 0;
    out[pos++] = '/';
    for (int i = 0; i < seg_count; i++)
    {
        int seg_len = (int)strlen(segments[i]);
        if (pos + (pos > 1 ? 1 : 0) + seg_len >= out_len)
            return 0;

        if (pos > 1)
            out[pos++] = '/';
        for (int j = 0; j < seg_len; j++)
            out[pos++] = segments[i][j];
    }

    if (pos == 1)
    {
        out[pos] = '\0';
        return 0; /* Root is not a valid file path. */
    }

    out[pos] = '\0';
    return 1;
}

/* Convert a shell path to a BranchFS filename. */
static int shell_to_fs_path(const char* shell_path, char* fs_path, int fs_len)
{
    if (!shell_path || !fs_path || fs_len <= 0)
        return 0;

    while (*shell_path == '/')
        shell_path++;
    if (*shell_path == '\0')
        return 0;

    return shell_copy_str(fs_path, shell_path, fs_len);
}

static int shell_resolve_to_fs(const char* input, char* fs_path, int fs_len)
{
    char abs_path[SHELL_PATH_MAX];
    if (!shell_resolve_path(input, abs_path, (int)sizeof(abs_path)))
        return 0;
    return shell_to_fs_path(abs_path, fs_path, fs_len);
}


/* ============================
   Shell I/O (via syscalls)
   ============================ */

static void shell_print(const char* str)
{
    sys_write(1, (long)str, (long)strlen(str), 0, 0, 0);
}

static void shell_println(const char* str)
{
    shell_print(str);
    shell_print("\n");
}

static void shell_read(char* buf, size_t maxlen)
{
    /* Delegate to tty_readline which provides: blocking keyboard reads,
       up/down arrow history navigation, and correct Ctrl+C handling. */
    tty_readline(buf, maxlen);
}

/* ============================
   Commands
   ============================ */

/* Print a uint64_t as decimal text */
static void shell_print_uint(uint64_t n)
{
    char buf[21];
    int i = 0;
    if (n == 0) { shell_print("0"); return; }
    while (n > 0) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; j--) tty_putchar(buf[j]);
}

static void cmd_whoami(void)
{
    /* Map uid to a username string */
    uint16_t uid = current ? current->uid : 0;
    if (uid == 0)
    {
        shell_println("root");
    }
    else
    {
        shell_print("user(");
        shell_print_uint((uint64_t)uid);
        shell_println(")");
    }
}

static void cmd_uptime(void)
{
    uint64_t ms      = timer_get_ms();
    uint64_t secs    = ms / 1000;
    uint64_t minutes = secs / 60;
    uint64_t hours   = minutes / 60;
    uint64_t days    = hours / 24;

    shell_print("up ");
    if (days > 0)
    {
        shell_print_uint(days);
        shell_print(days == 1 ? " day, " : " days, ");
    }
    shell_print_uint(hours % 24);
    shell_print(":");
    uint64_t m = minutes % 60;
    if (m < 10) shell_print("0");
    shell_print_uint(m);
    shell_print(":");
    uint64_t s = secs % 60;
    if (s < 10) shell_print("0");
    shell_print_uint(s);
    shell_print("  load avg: n/a");
    shell_println("");
}

static void cmd_echo(const char* args)
{
    shell_println(args);
}

static void cmd_help(void)
{
    shell_print("Available commands:\n");
    shell_print("  echo <text>  - Print text to screen\n");
    shell_print("  help         - Show this help message\n");
    shell_print("  clear        - Clear the screen\n");
    shell_print("  about        - About GraceOS\n");
    shell_print("  sysinfo      - Display system information\n");
    shell_print("  pmmstat      - Display PMM statistics\n");
    shell_print("  vmstat       - Display VMM statistics\n");
    shell_print("  segstat      - Display SASY segment statistics\n");
    shell_print("  ls           - List files\n");
    shell_print("  cd <path>     - Change directory\n");
    shell_print("  mkdir <name>  - Create directory\n");
    shell_print("  mkfile <name> <size> - Create file\n");
    shell_print("  cfdisk       - Partition the disk\n");
    shell_print("  find <name>  - Find file\n");
    shell_print("  rm <name>    - Delete file\n");
    shell_print("  cat <name>   - Show file contents\n");
    shell_print("  now [opts]   - Display current date/time\n");
    shell_print("  quill <file> - Edit a file\n");
    shell_print("  chivm        - Run ChibiVM REPL (step 1)\n");
    shell_print("  chivm build <src> <out.cmdt> - Compile source to CMDT\n");
    shell_print("  run <file.cmdt> - Execute CMDT via process manager\n");
    shell_print("  7c encode <zip> <src> [--overwrite] [--verbose]\n");
    shell_print("  7c decode <zip> [outdir] [--overwrite] [--verbose]\n");
    shell_print("  raygui       - Start GUI mode\n");
    shell_print("  whoami       - Print current user\n");
    shell_print("  uptime       - Show system uptime\n");
    shell_print("  snake        - Play Snake game\n");
    shell_print("  mplayer <file>      - Play WAV/MP3 audio\n");
    shell_print("  mplayer --sample    - Play embedded test sample\n");
    shell_print("  mplayer --beep      - PC speaker beep test\n");
    shell_print("  netstat      - Show network status\n");
    shell_print("  ifconfig [ip gw mask] - Show or set IP configuration\n");
    shell_print("  ping <ip> [count]     - ICMP echo test\n");
    shell_print("  fetch <ip> <path>     - HTTP GET request\n");
    shell_print("  shutdown     - Shutdown the system\n");
    shell_print("  reboot       - Reboot the system\n");
    shell_print("  halt         - Halt the CPU\n");
    shell_print("  exit         - Exit shell\n");
}

static void cmd_mkdir(const char* args)
{
    if (!args || *args == '\0')
    {
        shell_println("Usage: mkdir <name>");
        return;
    }

    char path[SHELL_PATH_MAX];
    if (!shell_resolve_path(args, path, (int)sizeof(path)))
    {
        shell_println("Invalid path");
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    if (!shell_to_fs_path(path, fs_path, (int)sizeof(fs_path)))
    {
        shell_println("Invalid path");
        return;
    }

    int result = bfs_create(&g_bfs, fs_path, 0);
    if (result == 0)
    {
        shell_print("Directory created: ");
        shell_println(path);
    }
    else if (result == -1)
    {
        shell_println("Already exists");
    }
    else if (result == -2)
    {
        shell_println("Not enough space");
    }
    else
    {
        shell_println("Failed to create directory");
    }
}

static void cmd_chdir(const char* args)
{
    if (!args || *args == '\0')
    {
        shell_copy_str(shell_cwd, "/", (int)sizeof(shell_cwd));
        return;
    }

    char path[SHELL_PATH_MAX];
    shell_resolve_path(args, path, (int)sizeof(path));

    /* shell_resolve_path writes "/" and returns 0 for root — accept it */
    if (path[0] == '/' && path[1] == '\0')
    {
        shell_copy_str(shell_cwd, "/", (int)sizeof(shell_cwd));
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    if (!shell_to_fs_path(path, fs_path, (int)sizeof(fs_path)))
    {
        shell_print("cd: ");
        shell_print(args);
        shell_println(": invalid path");
        return;
    }

    struct bfs_file_entry entry;
    if (bfs_find(&g_bfs, fs_path, &entry) != 0)
    {
        shell_print("cd: ");
        shell_print(args);
        shell_println(": no such directory");
        return;
    }

    shell_copy_str(shell_cwd, path, (int)sizeof(shell_cwd));
}

static void cmd_clear(void)
{
    tty_clear();
}

static void cmd_about(void)
{
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    shell_print("GraceOS v0.1.0\n");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("A simple operating system with syscall interface\n");
    shell_print("Built with FASM and Clang\n\n");
    shell_print("Syscalls:\n");
    shell_print("  write, read, exit, exec, alloc, free\n");
    shell_print("  open, close, list, getkey\n\n");
}

static void cmd_ls(const char* args)
{
    /* Determine the directory to list */
    char target_dir[SHELL_PATH_MAX];

    if (!args || *args == '\0')
    {
        /* No arguments: list current directory */
        shell_copy_str(target_dir, shell_cwd, (int)sizeof(target_dir));
    }
    else
    {
        /* Argument provided: resolve the path */
        if (!shell_resolve_path(args, target_dir, (int)sizeof(target_dir)))
        {
            shell_println("Invalid path");
            return;
        }
    }

    /* Convert shell path to BFS prefix (no leading slash) */
    char prefix[SHELL_PATH_MAX];
    if (target_dir[0] == '/' && target_dir[1] == '\0')
        prefix[0] = '\0';
    else
        shell_to_fs_path(target_dir, prefix, (int)sizeof(prefix));

    /* Show . and .. entries */
    tty_set_color(TTY_LIGHT_BLUE, TTY_BLACK);
    shell_println(".");
    if (prefix[0] != '\0')
        shell_println("..");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);

    /* List direct children. Entries are NUL-terminated, list ends with \0\0. */
    static char ls_buf[2048];
    int count = bfs_list_dir(&g_bfs, prefix, ls_buf, (int)sizeof(ls_buf));

    if (count <= 0)
        return;

    const char* p = ls_buf;
    while (*p)
    {
        /* Directories (trailing '/') in blue, files in default colour. */
        int len = 0;
        while (p[len]) len++;
        if (len > 0 && p[len - 1] == '/')
            tty_set_color(TTY_LIGHT_BLUE, TTY_BLACK);

        shell_println(p);

        if (len > 0 && p[len - 1] == '/')
            tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);

        p += len + 1; /* skip to next entry */
    }
}

static void cmd_mkfile(const char* args)
{
    if (!args || *args == '\0')
    {
        shell_println("Usage: mkfile <filename> <size>");
        return;
    }
    
    const char* filename = args;
    const char* size_str = args;
    
    while (*size_str && *size_str != ' ')
        size_str++;
    
    if (*size_str == '\0')
    {
        shell_println("Usage: mkfile <filename> <size>");
        return;
    }
    
    char fname[64];
    int len = size_str - filename;
    if (len >= 64) len = 63;
    for (int i = 0; i < len; i++)
        fname[i] = filename[i];
    fname[len] = '\0';
    
    while (*size_str == ' ')
        size_str++;
    
    uint64_t size = 0;
    while (*size_str >= '0' && *size_str <= '9')
    {
        size = size * 10 + (*size_str - '0');
        size_str++;
    }
    
    if (size == 0)
    {
        shell_println("Invalid size");
        return;
    }
    
    char path[SHELL_PATH_MAX];
    if (!shell_resolve_path(fname, path, (int)sizeof(path)))
    {
        shell_println("Invalid path");
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    shell_to_fs_path(path, fs_path, (int)sizeof(fs_path));

    int result = bfs_create(&g_bfs, fs_path, size);
    if (result == 0)
    {
        shell_print("File created: ");
        shell_println(path);
    }
    else if (result == -1)
    {
        shell_println("File already exists");
    }
    else if (result == -2)
    {
        shell_println("Not enough space");
    }
    else
    {
        shell_println("Failed to create file");
    }
}

static void cmd_find(const char* filename)
{
    if (!filename || *filename == '\0')
    {
        shell_println("Usage: find <filename>");
        return;
    }
    
    char path[SHELL_PATH_MAX];
    if (!shell_resolve_path(filename, path, (int)sizeof(path)))
    {
        shell_println("Invalid path");
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    shell_to_fs_path(path, fs_path, (int)sizeof(fs_path));

    long result = sys_open((long)fs_path, 0, 0, 0, 0, 0);
    if (result >= 0)
    {
        shell_print("Found: ");
        shell_println(path);
        sys_close(result, 0, 0, 0, 0, 0);
    }
    else
    {
        shell_print("File not found: ");
        shell_println(path);
    }
}

static void cmd_rm(const char* filename)
{
    if (!filename || *filename == '\0')
    {
        shell_println("Usage: rm <filename>");
        return;
    }
    
    char path[SHELL_PATH_MAX];
    if (!shell_resolve_path(filename, path, (int)sizeof(path)))
    {
        shell_println("Invalid path");
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    shell_to_fs_path(path, fs_path, (int)sizeof(fs_path));

    if (bfs_delete(&g_bfs, fs_path) == 0)
    {
        shell_print("Deleted: ");
        shell_println(path);
    }
    else
    {
        shell_print("File not found: ");
        shell_println(path);
    }
}

static void cmd_cat(const char* filename)
{
    if (!filename || *filename == '\0')
    {
        shell_println("Usage: cat <filename>");
        return;
    }

    char path[SHELL_PATH_MAX];
    if (!shell_resolve_path(filename, path, (int)sizeof(path)))
    {
        shell_println("Invalid path");
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    shell_to_fs_path(path, fs_path, (int)sizeof(fs_path));

    uint64_t size = 0;
    if (bfs_file_size(&g_bfs, fs_path, &size) != 0)
    {
        shell_print("File not found: ");
        shell_println(path);
        return;
    }

    static char cat_buf[16384];
    if (size >= sizeof(cat_buf))
    {
        shell_println("File too large for cat buffer");
        return;
    }

    uint64_t out_size = 0;
    if (bfs_read_file(&g_bfs, fs_path, cat_buf, sizeof(cat_buf) - 1, &out_size) != 0)
    {
        shell_println("Failed to read file");
        return;
    }

    cat_buf[out_size] = '\0';
    shell_print(cat_buf);
    shell_print("\n");
}

static void cmd_quill(const char* filename)
{
    if (!filename || *filename == '\0')
    {
        shell_println("Usage: quill <filename>");
        return;
    }

    char path[SHELL_PATH_MAX];
    if (!shell_resolve_path(filename, path, (int)sizeof(path)))
    {
        shell_println("Invalid path");
        return;
    }

    char fs_path[SHELL_PATH_MAX];
    shell_to_fs_path(path, fs_path, (int)sizeof(fs_path));

    quill_run(fs_path);
}

static void cmd_chivm_build(const char* args)
{
    if (!args || *args == '\0')
    {
        shell_println("Usage: chivm build <source> <output.cmdt>");
        return;
    }

    char src[64];
    char dst[64];
    int si = 0;
    int di = 0;
    const char* p = args;

    while (*p == ' ') p++;
    while (*p && *p != ' ' && si < (int)sizeof(src) - 1)
        src[si++] = *p++;
    src[si] = '\0';

    while (*p == ' ') p++;
    while (*p && *p != ' ' && di < (int)sizeof(dst) - 1)
        dst[di++] = *p++;
    dst[di] = '\0';

    if (src[0] == '\0' || dst[0] == '\0')
    {
        shell_println("Usage: chivm build <source> <output.cmdt>");
        return;
    }

    if (chivm_compile_file(src, dst) == 0)
    {
        shell_print("[chivm] compiled ");
        shell_print(src);
        shell_print(" -> ");
        shell_println(dst);
    }
    else
    {
        shell_println("[chivm] compile failed");
    }
}

static void cmd_run_exec(const char* filename)
{
    if (!filename || *filename == '\0')
    {
        shell_println("Usage: run <file.cmdt>");
        return;
    }

    if (chivm_exec_cmdt(filename) != 0)
        shell_println("[run] execution failed");
}

/* Print number in decimal */
static void shell_print_dec(uint64_t value)
{
    char buf[32];
    int i = 0;
    
    if (value == 0)
    {
        shell_print("0");
        return;
    }
    
    while (value > 0)
    {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    /* Reverse print */
    while (i > 0)
    {
        char c[2] = { buf[--i], '\0' };
        shell_print(c);
    }
}

static void cmd_pmmstat(void)
{
    if (!pmm_ready())
    {
        klog_warn("PMM is offline (using fallback allocator)");
        return;
    }
    
    uint64_t total = pmm_total();
    uint64_t free_mem = pmm_free();
    uint64_t used = total - free_mem;
    
    /* Convert to MB */
    uint64_t total_mb = total / (1024 * 1024);
    uint64_t free_mb = free_mem / (1024 * 1024);
    uint64_t used_mb = used / (1024 * 1024);
    
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[LOG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Total: ");
    shell_print_dec(total_mb);
    shell_println(" MB");
    
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[LOG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Free : ");
    shell_print_dec(free_mb);
    shell_println(" MB");
    
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[LOG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Used : ");
    shell_print_dec(used_mb);
    shell_println(" MB");
}

static void cmd_vmstat(void)
{
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[LOG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("VMM Status: ");
    
    if (vmm_ready())
    {
        shell_println("Online");
        
        uint64_t mapped = vmm_mapped_pages();
        uint64_t mapped_mb = (mapped * 4096) / (1024 * 1024);
        
        tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
        shell_print("[LOG  ] ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        shell_print("Mapped: ");
        shell_print_dec(mapped_mb);
        shell_println(" MB");
        
        tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
        shell_print("[LOG  ] ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        shell_print("Pages : ");
        shell_print_dec(mapped);
        shell_println("");
    }
    else
    {
        shell_println("Using boot page tables");
    }
    
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[LOG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_println("TLB: OK");
}

static void cmd_now(const char* args)
{
    struct now_config config;
    
    // Default: show date and time in 24-hour format
    config.flags = NOW_SHOW_DATE | NOW_SHOW_TIME | NOW_FORMAT_24H;
    
    // Parse arguments if provided
    if (args && *args != '\0')
    {
        // Simple argument parsing for shell context
        const char* p = args;
        int explicit_options = 0;
        
        while (*p)
        {
            // Skip whitespace
            while (*p == ' ') p++;
            if (*p == '\0') break;
            
            // Check for --date
            if (strstart(p, "--date"))
            {
                if (!explicit_options)
                {
                    config.flags &= ~(NOW_SHOW_DATE | NOW_SHOW_TIME);
                    explicit_options = 1;
                }
                config.flags |= NOW_SHOW_DATE;
                p += 6;
            }
            // Check for --time
            else if (strstart(p, "--time"))
            {
                if (!explicit_options)
                {
                    config.flags &= ~(NOW_SHOW_DATE | NOW_SHOW_TIME);
                    explicit_options = 1;
                }
                config.flags |= NOW_SHOW_TIME;
                p += 6;
            }
            // Check for --format=1 or --format=2
            else if (strstart(p, "--format="))
            {
                p += 9;
                if (*p == '1')
                {
                    config.flags &= ~(NOW_FORMAT_12H | NOW_FORMAT_24H);
                    config.flags |= NOW_FORMAT_12H;
                    p++;
                }
                else if (*p == '2')
                {
                    config.flags &= ~(NOW_FORMAT_12H | NOW_FORMAT_24H);
                    config.flags |= NOW_FORMAT_24H;
                    p++;
                }
                else
                {
                    shell_println("Error: Invalid format. Use --format=1 or --format=2");
                    return;
                }
            }
            // Check for --help
            else if (strstart(p, "--help") || strstart(p, "-h"))
            {
                shell_println("Usage: now [OPTIONS]");
                shell_println("Display current date and time");
                shell_println("");
                shell_println("Options:");
                shell_println("  --date         Show only date");
                shell_println("  --time         Show only time");
                shell_println("  --format=1     Use 12-hour format");
                shell_println("  --format=2     Use 24-hour format");
                shell_println("  --stream       Live updating display (Ctrl+C to stop)");
                return;
            }
            // Check for --stream
            else if (strstart(p, "--stream"))
            {
                config.flags |= NOW_STREAM;
                p += 8;
            }
            else
            {
                shell_print("Unknown option. Use 'now --help' for usage.\n");
                return;
            }
            
            // Skip whitespace
            while (*p == ' ') p++;
        }
    }
    
    // Check for stream mode
    if (config.flags & NOW_STREAM) {
        now_stream(&config);
        return;
    }
    
    // Display the time (one-shot)
    now_display(&config);
}

static void cmd_7c(const char* args)
{
    if (!args || *args == '\0')
    {
        shell_println("Usage: 7c encode <zip> <src> [--overwrite] [--verbose]");
        shell_println("       7c decode <zip> [outdir] [--overwrite] [--verbose]");
        return;
    }

    char arg_buf[CMD_BUFFER_SIZE];
    char* argv[SHELL_MAX_ARGS];
    int argc = 1;
    argv[0] = "7c";

    int parsed = shell_tokenize_args(args, arg_buf, (int)sizeof(arg_buf), &argv[1], SHELL_MAX_ARGS - 1);
    if (parsed < 0)
    {
        shell_println("7c: command too long");
        return;
    }
    argc += parsed;

    if (argc < 2 || argv[1][0] == '-')
    {
        shell_println("Usage: 7c encode <zip> <src> [--overwrite] [--verbose]");
        shell_println("       7c decode <zip> [outdir] [--overwrite] [--verbose]");
        return;
    }

    const char* mode = NULL;
    int nonflag_index = 0;
    char path_buf1[SHELL_PATH_MAX];
    char path_buf2[SHELL_PATH_MAX];
    char path_buf3[SHELL_PATH_MAX];

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
            continue;

        if (nonflag_index == 0)
        {
            mode = argv[i];
        }
        else if (nonflag_index == 1)
        {
            if (!shell_resolve_to_fs(argv[i], path_buf1, (int)sizeof(path_buf1)))
            {
                shell_println("7c: invalid path");
                return;
            }
            argv[i] = path_buf1;
        }
        else if (nonflag_index == 2)
        {
            if (!shell_resolve_to_fs(argv[i], path_buf2, (int)sizeof(path_buf2)))
            {
                shell_println("7c: invalid path");
                return;
            }
            argv[i] = path_buf2;
        }

        nonflag_index++;
    }

    if (!mode)
    {
        shell_println("7c: missing command");
        return;
    }

    if (strcmp(mode, "decode") == 0)
    {
        if (nonflag_index < 2)
        {
            shell_println("Usage: 7c decode <zip> [outdir] [--overwrite] [--verbose]");
            return;
        }

        if (nonflag_index == 2)
        {
            if (!shell_resolve_to_fs("result", path_buf3, (int)sizeof(path_buf3)))
            {
                shell_println("7c: invalid path");
                return;
            }
            if (argc >= SHELL_MAX_ARGS)
            {
                shell_println("7c: too many args");
                return;
            }
            argv[argc++] = path_buf3;
        }
    }
    else if (strcmp(mode, "encode") == 0)
    {
        if (nonflag_index < 3)
        {
            shell_println("Usage: 7c encode <zip> <src> [--overwrite] [--verbose]");
            return;
        }
    }
    else
    {
        shell_println("Usage: 7c encode <zip> <src> [--overwrite] [--verbose]");
        shell_println("       7c decode <zip> [outdir] [--overwrite] [--verbose]");
        return;
    }

    if (sevenzip_main(argc, argv) != 0)
        shell_println("7c: failed");
}

static void cmd_segstat(void)
{
    seg_stats_t stats;
    sasy_get_stats(&stats);
    
    /* Header */
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    shell_println("SASY Segment Allocator Statistics");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_println("----------------------------------");
    
    /* Total segments */
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Total Segments  : ");
    shell_print_dec(stats.total_segments);
    shell_println("");
    
    /* Resident segments */
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Resident        : ");
    shell_print_dec(stats.resident_segments);
    shell_println("");
    
    /* Swapped segments */
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Swapped         : ");
    shell_print_dec(stats.swapped_segments);
    shell_println("");
    
    /* Locked segments */
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Locked          : ");
    shell_print_dec(stats.locked_segments);
    shell_println("");
    
    /* Virtual memory */
    uint64_t virt_kb = stats.total_virtual_bytes / 1024;
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Virtual Memory  : ");
    shell_print_dec(virt_kb);
    shell_println(" KB");
    
    /* Physical memory */
    uint64_t phys_kb = stats.total_physical_bytes / 1024;
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Physical Memory : ");
    shell_print_dec(phys_kb);
    shell_println(" KB");
    
    /* Swapped memory */
    uint64_t swap_kb = stats.total_swapped_bytes / 1024;
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[SEG  ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Swapped Memory  : ");
    shell_print_dec(swap_kb);
    shell_println(" KB");
    
    /* Handle stats */
    uint32_t handles_total = handle_get_total();
    uint32_t handles_free = handle_get_free();
    
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[HNDL ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Handles Used    : ");
    shell_print_dec(handles_total);
    shell_println("");
    
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_print("[HNDL ] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("Handles Free    : ");
    shell_print_dec(handles_free);
    shell_println("");
}

/* ============================
   Network Helper Utilities
   ============================ */

/* Parse a dotted-decimal IPv4 string into host byte order.
   Returns 0 on failure. */
static uint32_t shell_parse_ip(const char *s)
{
    uint32_t result = 0;
    int octet = 0, shift = 24;
    while (*s)
    {
        if (*s >= '0' && *s <= '9')
        {
            octet = octet * 10 + (*s - '0');
            if (octet > 255) return 0;
        }
        else if (*s == '.')
        {
            result |= (uint32_t)octet << shift;
            shift -= 8;
            octet = 0;
        }
        else
        {
            return 0;
        }
        s++;
    }
    result |= (uint32_t)octet << shift;
    return result;
}

/* Print an IPv4 address in dotted-decimal (host byte order) */
static void shell_print_ip(uint32_t ip)
{
    shell_print_dec((ip >> 24) & 0xFF); shell_print(".");
    shell_print_dec((ip >> 16) & 0xFF); shell_print(".");
    shell_print_dec((ip >>  8) & 0xFF); shell_print(".");
    shell_print_dec( ip        & 0xFF);
}

/* Print a MAC address as xx:xx:xx:xx:xx:xx */
static void shell_print_mac(const uint8_t *mac)
{
    static const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++)
    {
        char buf[3];
        buf[0] = hex[mac[i] >> 4];
        buf[1] = hex[mac[i] & 0xF];
        buf[2] = '\0';
        shell_print(buf);
        if (i < 5) shell_print(":");
    }
}

/* ============================
   Network Commands
   ============================ */

static void cmd_netstat(void)
{
    if (!net_ready())
    {
        shell_println("Network: not available");
        return;
    }
    struct net_device *dev = net_get_device();
    if (!dev)
    {
        shell_println("No network device registered");
        return;
    }

    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    shell_println("Network Interface");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_print("  IP      : "); shell_print_ip(dev->ip);      shell_print("\n");
    shell_print("  Gateway : "); shell_print_ip(dev->gateway); shell_print("\n");
    shell_print("  Netmask : "); shell_print_ip(dev->netmask); shell_print("\n");
    shell_print("  MAC     : "); shell_print_mac(dev->mac);    shell_print("\n");
}

static void cmd_ifconfig(const char *args)
{
    if (!args || *args == '\0')
    {
        cmd_netstat();
        return;
    }

    /* Parse: <ip> <gateway> <netmask> */
    const char *p = args;

    /* IP */
    char tok[20];
    int t = 0;
    while (*p && *p != ' ' && t < 19) tok[t++] = *p++;
    tok[t] = '\0';
    uint32_t ip = shell_parse_ip(tok);
    if (ip == 0) { shell_println("Invalid IP address"); return; }

    while (*p == ' ') p++;

    /* Gateway */
    t = 0;
    while (*p && *p != ' ' && t < 19) tok[t++] = *p++;
    tok[t] = '\0';
    uint32_t gw = shell_parse_ip(tok);
    if (gw == 0) { shell_println("Invalid gateway address"); return; }

    while (*p == ' ') p++;

    /* Netmask */
    t = 0;
    while (*p && *p != ' ' && t < 19) tok[t++] = *p++;
    tok[t] = '\0';
    uint32_t nm = shell_parse_ip(tok);
    if (nm == 0) { shell_println("Invalid netmask"); return; }

    if (!net_get_device()) { shell_println("No network device"); return; }

    net_set_ip(ip, gw, nm);
    shell_print("IP set: ");
    shell_print_ip(ip);
    shell_print("  gw ");
    shell_print_ip(gw);
    shell_print("  mask ");
    shell_print_ip(nm);
    shell_print("\n");
}

static void cmd_ping(const char *args)
{
    if (!args || *args == '\0')
    {
        shell_println("Usage: ping <ip>");
        return;
    }

    /* Allow "ping <ip> [count]" */
    const char *p = args;
    char ipstr[20];
    int t = 0;
    while (*p && *p != ' ' && t < 19) ipstr[t++] = *p++;
    ipstr[t] = '\0';

    int count = 4;
    while (*p == ' ') p++;
    if (*p >= '1' && *p <= '9')
    {
        count = 0;
        while (*p >= '0' && *p <= '9') { count = count * 10 + (*p++ - '0'); }
        if (count <= 0 || count > 64) count = 4;
    }

    uint32_t ip = shell_parse_ip(ipstr);
    if (ip == 0) { shell_println("Invalid IP address"); return; }

    if (!net_ready()) { shell_println("Network not ready"); return; }

    shell_print("PING ");
    shell_print(ipstr);
    shell_print(": ");
    shell_print_dec((uint64_t)count);
    shell_println(" packets");

    int sent = 0, received = 0;
    uint16_t id = 0x4750; /* 'GP' — GraceOS Ping */

    for (int i = 1; i <= count; i++)
    {
        if (keyboard_consume_ctrl_c())
        {
            shell_println("^C");
            break;
        }

        sent++;
        int rtt = icmp_ping_one(ip, id, (uint16_t)i, 2000);
        if (rtt >= 0)
        {
            received++;
            shell_print("  seq=");
            shell_print_dec((uint64_t)i);
            shell_print(" time=");
            shell_print_dec((uint64_t)rtt);
            shell_println(" ms");
        }
        else
        {
            shell_print("  seq=");
            shell_print_dec((uint64_t)i);
            shell_println(" timeout");
        }
    }

    shell_print("--- ");
    shell_print(ipstr);
    shell_print(" --- ");
    shell_print_dec((uint64_t)sent);
    shell_print(" sent, ");
    shell_print_dec((uint64_t)received);
    shell_println(" received");
}

/* Static fetch output buffer (8 KB) */
static char fetch_out_buf[8192];

static void cmd_fetch(const char *args)
{
    if (!args || *args == '\0')
    {
        shell_println("Usage: fetch <ip> <path>");
        return;
    }

    const char *p = args;
    char host[64];
    int t = 0;
    while (*p && *p != ' ' && t < 63) host[t++] = *p++;
    host[t] = '\0';

    while (*p == ' ') p++;

    const char *path = (*p) ? p : "/";

    if (!net_ready()) { shell_println("Network not ready"); return; }

    shell_print("Fetching http://");
    shell_print(host);
    shell_print(path);
    shell_println(" ...");

    int n = http_fetch(host, path, fetch_out_buf, sizeof(fetch_out_buf) - 1);
    if (n < 0)
    {
        shell_println("Fetch failed");
        return;
    }
    fetch_out_buf[n] = '\0';
    shell_print(fetch_out_buf);
    if (n > 0 && fetch_out_buf[n - 1] != '\n')
        shell_print("\n");
    shell_print_dec((uint64_t)n);
    shell_println(" bytes received");
}

/* ============================
   Command Processor
   ============================ */

static void process_command(const char* cmd)
{
    while (*cmd == ' ')
        cmd++;
    
    if (*cmd == '\0')
        return;
    
    if (strcmp(cmd, "help") == 0)
        cmd_help();
    else if (strcmp(cmd, "clear") == 0)
        cmd_clear();
    else if (strcmp(cmd, "about") == 0)
        cmd_about();
    else if (strcmp(cmd, "sysinfo") == 0)
        cmd_sysinfo();
    else if (strcmp(cmd, "pmmstat") == 0)
        cmd_pmmstat();
    else if (strcmp(cmd, "vmstat") == 0)
        cmd_vmstat();
    else if (strcmp(cmd, "segstat") == 0)
        cmd_segstat();
    else if (strstart(cmd, "echo "))
        cmd_echo(get_args(cmd));
    else if (strcmp(cmd, "echo") == 0)
        shell_println("");
    else if (strcmp(cmd, "ls") == 0)
        cmd_ls("");
    else if (strstart(cmd, "ls "))
        cmd_ls(get_args(cmd));
    else if (strcmp(cmd, "cfdisk") == 0)
        shell_println("cfdisk: temporarily disabled");
    else if (strcmp(cmd, "mkdir") == 0)
        cmd_mkdir("");
    else if (strstart(cmd, "mkdir "))
        cmd_mkdir(get_args(cmd));
    else if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "chdir") == 0)
        cmd_chdir("");
    else if (strstart(cmd, "cd "))
        cmd_chdir(get_args(cmd));
    else if (strstart(cmd, "chdir "))
        cmd_chdir(get_args(cmd));
    else if (strstart(cmd, "mkfile "))
        cmd_mkfile(get_args(cmd));
    else if (strstart(cmd, "find "))
        cmd_find(get_args(cmd));
    else if (strstart(cmd, "rm "))
        cmd_rm(get_args(cmd));
    else if (strstart(cmd, "cat "))
        cmd_cat(get_args(cmd));
    else if (strstart(cmd, "quill "))
        cmd_quill(get_args(cmd));
    else if (strcmp(cmd, "chivm") == 0)
        chivm_run();
    else if (strstart(cmd, "chivm build "))
        cmd_chivm_build(get_args(get_args(cmd)));
    else if (strstart(cmd, "run "))
        cmd_run_exec(get_args(cmd));
    else if (strcmp(cmd, "run") == 0)
        shell_println("Usage: run <program.cmdt>");
    else if (strcmp(cmd, "raygui") == 0)
        gui_run();
    else if (strcmp(cmd, "whoami") == 0)
        cmd_whoami();
    else if (strcmp(cmd, "uptime") == 0)
        cmd_uptime();
    else if(strcmp(cmd, "snake") == 0)
        snake_run();
    else if (strcmp(cmd, "mplayer") == 0)
        mplayer_run("");
    else if (strstart(cmd, "mplayer "))
        mplayer_run(get_args(cmd));
    else if (strcmp(cmd, "now") == 0)
        cmd_now("");
    else if (strstart(cmd, "now "))
        cmd_now(get_args(cmd));
    else if (strcmp(cmd, "7c") == 0)
        cmd_7c("");
    else if (strstart(cmd, "7c "))
        cmd_7c(get_args(cmd));
    else if (strcmp(cmd, "netstat") == 0)
        cmd_netstat();
    else if (strcmp(cmd, "ifconfig") == 0)
        cmd_ifconfig("");
    else if (strstart(cmd, "ifconfig "))
        cmd_ifconfig(get_args(cmd));
    else if (strstart(cmd, "ping "))
        cmd_ping(get_args(cmd));
    else if (strcmp(cmd, "ping") == 0)
        shell_println("Usage: ping <ip> [count]");
    else if (strstart(cmd, "fetch "))
        cmd_fetch(get_args(cmd));
    else if (strcmp(cmd, "fetch") == 0)
        shell_println("Usage: fetch <ip> <path>");
    else if (strcmp(cmd, "shutdown") == 0)
    {
        tty_set_color(TTY_YELLOW, TTY_BLACK);
        shell_print("\n[POWER] ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        shell_println("System shutting down...");
        shell_println("Goodbye!\n");
        power_shutdown();
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
        tty_set_color(TTY_YELLOW, TTY_BLACK);
        shell_print("\n[POWER] ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        shell_println("System rebooting...\n");
        power_reboot();
    }
    else if (strcmp(cmd, "halt") == 0)
    {
        tty_set_color(TTY_YELLOW, TTY_BLACK);
        shell_print("\n[POWER] ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        shell_println("System halting...");
        shell_println("It is now safe to turn off your computer.\n");
        power_halt();
    }
    else if (strcmp(cmd, "exit") == 0)
    {
        shell_println("Goodbye!");
        sys_exit(0, 0, 0, 0, 0, 0);
    }
    else
    {
        shell_print("Unknown command: ");
        shell_println(cmd);
        shell_println("Type 'help' for a list of commands.");
    }
}

/* ============================
   Shell Entry Points
   ============================ */

void shell_init(void)
{
    hist_init();
    shell_copy_str(shell_cwd, "/", (int)sizeof(shell_cwd));
    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    shell_println("Welcome to GraceOS!");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    shell_println("Type 'help' for a list of commands.\n");
}

void shell_run(void)
{
    while (1)
    {
        tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
        shell_print("GraceOS> ");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        
        shell_read(cmd_buffer, CMD_BUFFER_SIZE);

        /* Fresh command execution starts with cleared interrupt latch. */
        keyboard_clear_ctrl_c();
        
        process_command(cmd_buffer);
    }
}
