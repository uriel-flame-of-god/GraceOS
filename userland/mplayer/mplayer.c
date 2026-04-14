// ============================
// GraceOS mplayer — Media Player
// Userland CLI for VoiceBoxSystem
// Runs in kernel mode (shell-embedded) for Phase 1
// ============================

#include "mplayer.h"
#include "../../kernel/audio/voicebox.h"
#include "../../kernel/audio/voicebox_server.h"
#include "../../drivers/video/tty.h"
#include "../../drivers/audio/hda.h"
#include "../../lib/libc/string.h"

/* ============================
   Helpers
   ============================ */

static void mp_print(const char* s)
{
    tty_print(s);
}

static void mp_println(const char* s)
{
    tty_print(s);
    tty_print("\n");
}

static int mp_strcmp(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int mp_strstart(const char* s, const char* prefix)
{
    while (*prefix)
    {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static const char* mp_skip_spaces(const char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int mp_atoi(const char* s)
{
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { val = val*10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

/* ============================
   Usage
   ============================ */

static void mp_usage(void)
{
    tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
    mp_println("mplayer — GraceOS Media Player (VoiceBoxSystem)");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    mp_println("Usage:");
    mp_println("  mplayer <file>           Play a WAV or MP3 file from BranchFS");
    mp_println("  mplayer --sample         Play the embedded test sample");
    mp_println("  mplayer --test           Alias for --sample");
    mp_println("  mplayer --info <file>    Show file metadata without playing");
    mp_println("  mplayer --beep           Play a PC speaker beep (no HDA needed)");
    mp_println("  mplayer --switch <isa|sb16|ac97|hda|auto>  Select audio backend");
    mp_println("  mplayer --help           Show this help");
    mp_println("");
    mp_println("Options (with <file>):");
    mp_println("  --volume <0-100>         Set volume before playback");
    mp_println("  --loop                   Repeat playback indefinitely");
}

/* ============================
   Info (metadata display)
   ============================ */

static void mp_show_info(const char* path)
{
    /* Basic WAV info via bfs */
    tty_set_color(TTY_YELLOW, TTY_BLACK);
    mp_print("[mplayer] File: ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    mp_println(path);

    /* Detect format */
    uint32_t plen = 0;
    while (path[plen]) plen++;

    const char* fmt = "unknown";
    if (plen >= 4 && path[plen-3]=='m' && path[plen-2]=='p' && path[plen-1]=='3')
        fmt = "MP3";
    else if (plen >= 3 && path[plen-3]=='w' && path[plen-2]=='a' && path[plen-1]=='v')
        fmt = "WAV (RIFF PCM)";

    tty_set_color(TTY_YELLOW, TTY_BLACK);
    mp_print("[mplayer] Format: ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    mp_println(fmt);
    mp_println("[mplayer] (detailed metadata requires file read — use mplayer <file> to play)");
}

/* ============================
   Main entry point
   ============================ */

void mplayer_run(const char* args)
{
    args = mp_skip_spaces(args);
    int requested_volume = -1;
    int switch_changed = 0;

    /* No arguments */
    if (!args || *args == '\0')
    {
        mp_usage();
        return;
    }

    /* Optional global prefixes parsed left-to-right.
       Examples:
         mplayer --switch isa
         mplayer --switch hda --volume 35 --sample */
    for (;;)
    {
        if (mp_strstart(args, "--volume "))
        {
            const char* p = mp_skip_spaces(args + 9);
            int v = mp_atoi(p);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            requested_volume = v;

            while (*p && *p != ' ') p++;
            args = mp_skip_spaces(p);

            hda_set_volume((uint8_t)requested_volume);
            tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
            mp_print("[mplayer] Volume set to ");
            {
                char numbuf[4];
                int idx = 0;
                int n = requested_volume;
                if (n >= 100) { numbuf[idx++] = '1'; numbuf[idx++] = '0'; numbuf[idx++] = '0'; }
                else if (n >= 10) { numbuf[idx++] = (char)('0' + (n / 10)); numbuf[idx++] = (char)('0' + (n % 10)); }
                else { numbuf[idx++] = (char)('0' + n); }
                numbuf[idx] = '\0';
                mp_print(numbuf);
            }
            mp_println("%");
            tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            continue;
        }

        if (mp_strstart(args, "--switch "))
        {
            const char* p = mp_skip_spaces(args + 9);
            const char* end = p;
            while (*end && *end != ' ') end++;

            if ((end - p) == 3 && p[0] == 'i' && p[1] == 's' && p[2] == 'a')
            {
                voicebox_set_backend(AUDIO_BACKEND_ISA);
                switch_changed = 1;
                tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
                mp_println("[mplayer] Backend switched to ISA speaker");
                tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            }
            else if ((end - p) == 3 && p[0] == 'h' && p[1] == 'd' && p[2] == 'a')
            {
                voicebox_set_backend(AUDIO_BACKEND_HDA);
                switch_changed = 1;
                tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
                mp_println("[mplayer] Backend switched to HDA");
                tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            }
            else if ((end - p) == 4 && p[0] == 's' && p[1] == 'b' && p[2] == '1' && p[3] == '6')
            {
                voicebox_set_backend(AUDIO_BACKEND_SB16);
                switch_changed = 1;
                tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
                mp_println("[mplayer] Backend switched to SB16");
                tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            }
            else if ((end - p) == 4 && p[0] == 'a' && p[1] == 'c' && p[2] == '9' && p[3] == '7')
            {
                voicebox_set_backend(AUDIO_BACKEND_AC97);
                switch_changed = 1;
                tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
                mp_println("[mplayer] Backend switched to AC97");
                tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            }
            else if ((end - p) == 4 && p[0] == 'a' && p[1] == 'u' && p[2] == 't' && p[3] == 'o')
            {
                voicebox_set_backend(AUDIO_BACKEND_AUTO);
                switch_changed = 1;
                tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
                mp_println("[mplayer] Backend switched to AUTO");
                tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            }
            else
            {
                tty_set_color(TTY_RED, TTY_BLACK);
                mp_println("[mplayer] Invalid backend. Use --switch isa|sb16|ac97|hda|auto");
                tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
                return;
            }

            args = mp_skip_spaces(end);
            continue;
        }

        break;
    }

    if (*args == '\0')
    {
        if (switch_changed || requested_volume >= 0)
            return;
        mp_usage();
        return;
    }

    /* --help */
    if (mp_strcmp(args, "--help") == 0 || mp_strcmp(args, "-h") == 0)
    {
        mp_usage();
        return;
    }

    /* --sample / --test  (play embedded sample) */
    if (mp_strcmp(args, "--sample") == 0 || mp_strcmp(args, "--test") == 0)
    {
        if (!voicebox_ready())
        {
            mp_println("[mplayer] VoiceBox not initialized");
            return;
        }

        tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
        mp_println("[mplayer] Playing embedded sample...");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);

        if (requested_volume >= 0)
        {
            hda_set_volume((uint8_t)requested_volume);
            mp_println("[mplayer] Sample mode: using requested volume");
        }
        else
        {
            mp_println("[mplayer] Sample mode: using runtime inferred settings");
        }

        uint32_t sample_sz = voicebox_sample_mp3_size();
        if (sample_sz > 0)
        {
            mp_print("[mplayer] Embedded MP3 size: ");
            /* Simple decimal print */
            char numbuf[16];
            int idx = 0;
            uint32_t n = sample_sz;
            if (n == 0) { numbuf[idx++] = '0'; }
            else {
                char tmp[16]; int ti = 0;
                while (n > 0) { tmp[ti++] = '0' + (int)(n % 10); n /= 10; }
                while (ti > 0) numbuf[idx++] = tmp[--ti];
            }
            numbuf[idx] = '\0';
            mp_print(numbuf);
            mp_println(" bytes");
        }
        else
        {
            mp_println("[mplayer] No embedded sample — synthesising test tone (440 Hz)");
        }

        int rc = voicebox_play_sample();
        if (rc == 0)
        {
            tty_set_color(TTY_GREEN, TTY_BLACK);
            mp_println("[mplayer] Playback complete.");
        }
        else
        {
            tty_set_color(TTY_RED, TTY_BLACK);
            mp_println("[mplayer] Playback failed.");
        }
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        return;
    }

    /* --beep  (PC speaker, no HDA required) */
    if (mp_strcmp(args, "--beep") == 0)
    {
        if (requested_volume >= 0)
            hda_set_volume((uint8_t)requested_volume);

        tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
        mp_println("[mplayer] Beeping (440 Hz, 500 ms)...");
        tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        hda_beep(440, 500);
        mp_println("[mplayer] Done.");
        return;
    }

    /* --info <file> */
    if (mp_strstart(args, "--info "))
    {
        const char* path = mp_skip_spaces(args + 7);
        mp_show_info(path);
        return;
    }

    /* Parse optional flags before the filename */
    uint32_t volume   = (requested_volume >= 0) ? (uint32_t)requested_volume : 80;
    int      loop     = 0;
    const char* filename = 0;

    const char* p = args;
    while (*p)
    {
        if (mp_strstart(p, "--volume "))
        {
            p += 9;
            p = mp_skip_spaces(p);
            int v = mp_atoi(p);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            volume = (uint32_t)v;
            while (*p && *p != ' ') p++;
        }
        else if (mp_strcmp(p, "--loop") == 0 || mp_strstart(p, "--loop "))
        {
            loop = 1;
            p += 6;
        }
        else if (*p != '-')
        {
            filename = p;
            break;
        }
        else
        {
            /* Unknown flag — skip word */
            while (*p && *p != ' ') p++;
        }
        p = mp_skip_spaces(p);
    }

    if (!filename || *filename == '\0')
    {
        mp_println("[mplayer] Error: no file specified");
        mp_usage();
        return;
    }

    /* Validate VoiceBox */
    if (!voicebox_ready())
    {
        mp_println("[mplayer] Error: VoiceBox not initialized");
        return;
    }

    tty_set_color(TTY_LIGHT_GREEN, TTY_BLACK);
    mp_print("[mplayer] Playing: ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    mp_println(filename);

    /* Apply global device volume before playback. */
    hda_set_volume((uint8_t)volume);

    do {
        int rc = voicebox_play_file(filename);
        if (rc < 0)
        {
            tty_set_color(TTY_RED, TTY_BLACK);
            mp_print("[mplayer] Error: could not play '");
            mp_print(filename);
            mp_println("'");
            mp_println("  Possible reasons:");
            mp_println("    - File not found in BranchFS");
            mp_println("    - Unsupported format (use .wav or .mp3)");
            mp_println("    - VoiceBox stream allocation failed");
            tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
            break;
        }

        if (loop)
        {
            tty_set_color(TTY_LIGHT_CYAN, TTY_BLACK);
            mp_println("[mplayer] Looping... (not yet interruptible in Phase 1)");
            tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
        }
    } while (loop);

    tty_set_color(TTY_GREEN, TTY_BLACK);
    mp_println("[mplayer] Done.");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
}
