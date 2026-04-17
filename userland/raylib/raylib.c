// ============================
// GraceOS Raylib Compatibility Shim
// ============================

#include "raylib.h"
#include "../../lib/libc/float.h"
#include "../../lib/libc/string.h"
#include "../../lib/libgrace/grace.h"

/* Frame timing and loop state */
static uint64_t frame_start_time = 0;
static uint64_t frame_end_time = 0;
static float frame_time_sec = 0.016f;
static int frame_fps = 60;
static int target_fps = 60;
static uint64_t frame_count = 0;
static int frame_active = 0;

/* ============================
   Raylib Input State
   ============================ */

#define MAX_KEYS 512

// Special key constants (from keyboard driver)
#define KEY_SPECIAL_UP    0x80
#define KEY_SPECIAL_DOWN  0x81
#define KEY_SPECIAL_LEFT  0x82
#define KEY_SPECIAL_RIGHT 0x83
#define KEY_LSHIFT        0x2A
#define KEY_RSHIFT        0x36
#define KEY_LCTRL         0x1D
#define KEY_RALT          0x38

static struct {
    uint8_t keys[MAX_KEYS];
    uint8_t prev_keys[MAX_KEYS];
    uint8_t key_was_pressed[MAX_KEYS];
    uint8_t key_released[MAX_KEYS];
    uint64_t key_last_seen_ms[MAX_KEYS];
    int key_queue[MAX_KEYS];
    int key_queue_count;
    int key_queue_read;
    int char_queue[64];
    int char_queue_count;
    int char_queue_read;
} raylib_state = {0};

static const uint64_t key_hold_window_ms = 120;

/* ============================
   GraceOS to Raylib Key Mapping
   ============================ */

static int graceos_to_raylib_key(int graceos_key)
{
    switch (graceos_key) {
        case 0x01: return KEY_ESCAPE;      /* KEY_ESCAPE */
        case 0x0E: return KEY_BACKSPACE;   /* KEY_BACKSPACE */
        case 0x0F: return KEY_TAB;         /* KEY_TAB */
        case 0x1C: return KEY_ENTER;       /* KEY_ENTER */
        case 0x3B: return KEY_F1;          /* KEY_F1 */
        case 0x3C: return KEY_F2;          /* KEY_F2 */
        case 0x3D: return KEY_F3;          /* KEY_F3 */
        case 0x3E: return KEY_F4;          /* KEY_F4 */
        case 0x3F: return KEY_F5;          /* KEY_F5 */
        case 0x40: return KEY_F6;          /* KEY_F6 */
        case 0x41: return KEY_F7;          /* KEY_F7 */
        case 0x42: return KEY_F8;          /* KEY_F8 */
        case 0x43: return KEY_F9;          /* KEY_F9 */
        case 0x44: return KEY_F10;         /* KEY_F10 */
        case KEY_SPECIAL_UP:    return KEY_UP;
        case KEY_SPECIAL_DOWN:  return KEY_DOWN;
        case KEY_SPECIAL_LEFT:  return KEY_LEFT;
        case KEY_SPECIAL_RIGHT: return KEY_RIGHT;
        case KEY_LSHIFT:        return KEY_LEFT_SHIFT;
        case KEY_RSHIFT:        return KEY_RIGHT_SHIFT;
        case KEY_LCTRL:         return KEY_LEFT_CONTROL;
        case KEY_RALT:          return KEY_RIGHT_ALT;
        default:
            /* Handle arrow keys that might come through as regular scancodes */
            if (graceos_key == 0x48) return KEY_UP;      /* UP arrow scancode */
            if (graceos_key == 0x50) return KEY_DOWN;    /* DOWN arrow scancode */
            if (graceos_key == 0x4B) return KEY_LEFT;    /* LEFT arrow scancode */
            if (graceos_key == 0x4D) return KEY_RIGHT;   /* RIGHT arrow scancode */
            
            /* ASCII printable - convert lowercase to uppercase for key constants */
            if (graceos_key >= 'a' && graceos_key <= 'z') {
                return graceos_key - ('a' - 'A');  // Convert to uppercase
            }
            if (graceos_key >= 'A' && graceos_key <= 'Z') {
                return graceos_key;
            }
            if (graceos_key >= '0' && graceos_key <= '9') {
                return graceos_key;
            }
            return KEY_NULL;
    }
}

/* ============================
   Input + Frame Loop System
   ============================ */

static void push_key_queue(int key)
{
    if (raylib_state.key_queue_count < MAX_KEYS)
    {
        raylib_state.key_queue[raylib_state.key_queue_count++] = key;
    }
}

static void push_char_queue(int c)
{
    if (raylib_state.char_queue_count < (int)(sizeof(raylib_state.char_queue) / sizeof(raylib_state.char_queue[0])))
    {
        raylib_state.char_queue[raylib_state.char_queue_count++] = c;
    }
}

static void update_input_frame(void)
{
    uint64_t now = time_ms();

    memcpy(raylib_state.prev_keys, raylib_state.keys, MAX_KEYS);
    memset(raylib_state.key_was_pressed, 0, MAX_KEYS);
    memset(raylib_state.key_released, 0, MAX_KEYS);

    for (int i = 0; i < MAX_KEYS; i++)
    {
        if (raylib_state.key_last_seen_ms[i] != 0 && (now - raylib_state.key_last_seen_ms[i]) <= key_hold_window_ms)
            raylib_state.keys[i] = 1;
        else
            raylib_state.keys[i] = 0;
    }

    raylib_state.key_queue_count = 0;
    raylib_state.key_queue_read = 0;
    raylib_state.char_queue_count = 0;
    raylib_state.char_queue_read = 0;

    /* Non-blocking input using read(0, ...) which returns 0 when no input available */
    char c;
    long result = read(0, &c, 1);
    while (result > 0)
    {
        unsigned char raw = (unsigned char)c;
        int raylib_key = graceos_to_raylib_key(raw);

        if (raylib_key > KEY_NULL && raylib_key < MAX_KEYS)
        {
            raylib_state.key_last_seen_ms[raylib_key] = now;
            raylib_state.keys[raylib_key] = 1;

            if (!raylib_state.prev_keys[raylib_key])
                raylib_state.key_was_pressed[raylib_key] = 1;

            push_key_queue(raylib_key);
        }

        if (raw >= 32 && raw <= 126)
            push_char_queue(raw);

        result = read(0, &c, 1);
    }

    for (int i = 0; i < MAX_KEYS; i++)
    {
        if (raylib_state.prev_keys[i] && !raylib_state.keys[i])
            raylib_state.key_released[i] = 1;
    }
}

/* ============================
   Window Functions
   ============================ */

static int window_initialized = 0;
int window_should_close = 0;
static int screen_width = 800;
static int screen_height = 600;

void InitWindow(int width, int height, const char* title)
{
    (void)title;
    if (width > 0) screen_width = width;
    if (height > 0) screen_height = height;

    struct fb_info info;
    if (fb_get_info(&info) == 0)
    {
        screen_width = (int)info.width;
        screen_height = (int)info.height;
    }

    memset(&raylib_state, 0, sizeof(raylib_state));
    window_initialized = 1;
    window_should_close = 0;
    frame_start_time = time_ms();
    frame_end_time = frame_start_time;
    frame_time_sec = 0.016f;
    frame_fps = 60;
    frame_count = 0;
    frame_active = 0;
}

void CloseWindow(void)
{
    window_initialized = 0;
    frame_active = 0;
}

int WindowShouldClose(void)
{
    if (!window_initialized)
        return 1;
    return window_should_close;
}

/* ============================
   Timing Functions
   ============================ */

void SetTargetFPS(int fps)
{
    if (fps < 1) fps = 1;
    if (fps > 240) fps = 240;
    target_fps = fps;
}

float GetFrameTime(void)
{
    return frame_time_sec;
}

double GetTime(void)
{
    return (double)time_ms() / 1000.0;
}

int GetFPS(void)
{
    return frame_fps;
}

int IsWindowReady(void)
{
    return window_initialized;
}

int GetScreenWidth(void)
{
    return screen_width;
}

int GetScreenHeight(void)
{
    return screen_height;
}

int IsKeyPressed(int key)
{
    if (key >= 0 && key < MAX_KEYS)
        return raylib_state.key_was_pressed[key] ? 1 : 0;
    return 0;
}

int IsKeyDown(int key)
{
    if (key >= 0 && key < MAX_KEYS)
        return raylib_state.keys[key] ? 1 : 0;
    return 0;
}

int IsKeyReleased(int key)
{
    if (key >= 0 && key < MAX_KEYS)
        return raylib_state.key_released[key] ? 1 : 0;
    return 0;
}

int IsKeyUp(int key)
{
    return !IsKeyDown(key);
}

/* ============================
   Drawing Functions (Direct Framebuffer Syscalls)
   ============================ */

/* ============================
   Drawing Functions (Direct Framebuffer Syscalls)
   ============================ */



// Standard 8x8 font for ASCII 32-127 (96 chars * 8 bytes = 768 bytes)
// We will generate this programmatically or include a small one. 
// For this environment, I'll include a basic set for 0-9, A-Z, a-z, and punctuation.
static const uint8_t font_basic[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // SPACE
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // "
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // #
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // $
    {0x00,0xC6,0xcc,0x18,0x30,0x66,0xC6,0x00}, // %
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // &
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // (
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // )
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
    {0x00,0x06,0x0C,0x18,0x30,0x60,0x00,0x00}, // /
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
    {0x18,0x18,0x38,0x18,0x18,0x18,0x3C,0x00}, // 1
    {0x3C,0x66,0x60,0x30,0x0C,0x06,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x06,0x0E,0x1E,0x36,0x66,0x7F,0x06,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}, // 9
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // :
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ;
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // <
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // =
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // >
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, // ?
    {0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00}, // @
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // A
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, // E
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // F
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, // G
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // I
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, // J
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // K
    {0xF0,0x60,0x60,0x60,0x60,0x60,0xF0,0x00}, // L
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, // M
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // N
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P
    {0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x04}, // Q
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x63,0x00}, // R
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // S
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // V
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // W
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // X
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // Y
    {0xFE,0x06,0x0C,0x18,0x30,0x60,0xFE,0x00}, // Z
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // [
    {0x00,0x60,0x30,0x18,0x0C,0x06,0x00,0x00}, // backslash
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ]
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // _
    {0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00}, // `
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // a
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // b
    {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00}, // c
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // d
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // e
    {0x1C,0x20,0x78,0x20,0x20,0x20,0x20,0x00}, // f
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // g
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // h
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // i
    {0x0C,0x00,0x0C,0x0C,0x0C,0x0C,0x6C,0x38}, // j
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // k
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // l
    {0x00,0x00,0x6C,0xFE,0xFE,0xD6,0xC6,0x00}, // m
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // n
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // o
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // p
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // q
    {0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x00}, // r
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // s
    {0x20,0x20,0x78,0x20,0x20,0x20,0x18,0x00}, // t
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // u
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // v
    {0x00,0x00,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // w
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // x
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x0C,0x78}, // y
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // z
    {0x0C,0x18,0x18,0x70,0x18,0x18,0x0C,0x00}, // {
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18}, // |
    {0x30,0x18,0x18,0x0E,0x18,0x18,0x30,0x00}, // }
    {0x31,0x64,0x45,0x00,0x00,0x00,0x00,0x00}, // ~ (approx)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // DEL
};

/* Convert Color to framebuffer format
 * TTY uses 0xFFRRGGBB format which is ABGR byte order
 * On little-endian x86, this becomes [BB][GG][RR][FF] in memory
 * We need to match this format: alpha=FF, then B, G, R
 */
static inline uint32_t color_to_argb(Color color)
{
    // Format: 0xAABBGGRR (ABGR) to match TTY
    return ((uint32_t)color.a << 24) | ((uint32_t)color.b << 16) | ((uint32_t)color.g << 8) | color.r;
}

void BeginDrawing(void)
{
    if (!window_initialized)
        return;

    uint64_t now = time_ms();
    if (frame_end_time != 0 && target_fps > 0)
    {
        uint64_t target_ms = 1000ULL / (uint64_t)target_fps;
        uint64_t elapsed = now - frame_end_time;
        if (elapsed < target_ms)
            sleep_ms((int)(target_ms - elapsed));
    }

    frame_start_time = time_ms();
    update_input_frame();
    frame_active = 1;

    if (raylib_state.key_was_pressed[KEY_ESCAPE])
        window_should_close = 1;
}

void EndDrawing(void)
{
    if (!window_initialized)
        return;

    grace_fb_present();

    frame_end_time = time_ms();
    if (frame_active)
    {
        uint64_t dt_ms = frame_end_time - frame_start_time;
        if (dt_ms == 0) dt_ms = 1;

        frame_time_sec = (float)dt_ms / 1000.0f;
        if (frame_time_sec < 0.001f) frame_time_sec = 0.001f;
        if (frame_time_sec > 0.250f) frame_time_sec = 0.250f;

        frame_fps = (int)(1.0f / frame_time_sec);
        frame_count++;
        frame_active = 0;
    }
}

void ClearBackground(Color color)
{
    grace_fb_clear(color_to_argb(color));
}

void DrawRectangle(int x, int y, int width, int height, Color color)
{
    grace_fb_fill_rect(x, y, width, height, color_to_argb(color));
}

void DrawColor(Color color)
{
    ClearBackground(color);
}

void DrawPixel(int x, int y, Color color)
{
    grace_fb_put_pixel(x, y, color_to_argb(color));
}

void DrawRectangleLines(int x, int y, int width, int height, Color color)
{
    // Draw rectangle lines using grace_fb_fill_rect for each side
    uint32_t c = color_to_argb(color);
    grace_fb_fill_rect(x, y, width, 1, c);           // Top
    grace_fb_fill_rect(x, y + height - 1, width, 1, c);  // Bottom
    grace_fb_fill_rect(x, y, 1, height, c);          // Left
    grace_fb_fill_rect(x + width - 1, y, 1, height, c);   // Right
}

void DrawLine(int startX, int startY, int endX, int endY, Color color)
{
    grace_fb_line(startX, startY, endX, endY, color_to_argb(color));
}

void DrawCircle(int centerX, int centerY, float radius, Color color)
{
    grace_fb_circle(centerX, centerY, (int)radius, color_to_argb(color));
}

void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color)
{
    DrawLine((int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, color);
    DrawLine((int)v2.x, (int)v2.y, (int)v3.x, (int)v3.y, color);
    DrawLine((int)v3.x, (int)v3.y, (int)v1.x, (int)v1.y, color);
}

void DrawTriangleFilled(Vector2 v1, Vector2 v2, Vector2 v3, Color color)
{
    int min_x = (int)v1.x;
    int max_x = (int)v1.x;
    int min_y = (int)v1.y;
    int max_y = (int)v1.y;

    if ((int)v2.x < min_x) min_x = (int)v2.x;
    if ((int)v3.x < min_x) min_x = (int)v3.x;
    if ((int)v2.x > max_x) max_x = (int)v2.x;
    if ((int)v3.x > max_x) max_x = (int)v3.x;
    if ((int)v2.y < min_y) min_y = (int)v2.y;
    if ((int)v3.y < min_y) min_y = (int)v3.y;
    if ((int)v2.y > max_y) max_y = (int)v2.y;
    if ((int)v3.y > max_y) max_y = (int)v3.y;

    float area = (v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y);
    if (area == 0.0f)
        return;

    for (int y = min_y; y <= max_y; y++)
    {
        for (int x = min_x; x <= max_x; x++)
        {
            float w0 = (v2.x - v1.x) * ((float)y - v1.y) - (v2.y - v1.y) * ((float)x - v1.x);
            float w1 = (v3.x - v2.x) * ((float)y - v2.y) - (v3.y - v2.y) * ((float)x - v2.x);
            float w2 = (v1.x - v3.x) * ((float)y - v3.y) - (v1.y - v3.y) * ((float)x - v3.x);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                DrawPixel(x, y, color);
        }
    }
}

void DrawText(const char* text, int x, int y, int fontSize, Color color)
{
    if (!text) return;
    
    uint32_t c = color_to_argb(color);
    int curr_x = x;
    int curr_y = y;
    int scale = fontSize / 8;
    if (scale < 1) scale = 1;
    
    while (*text) {
        char ch = *text++;
        
        if (ch == '\n') {
            curr_y += 8 * scale;
            curr_x = x;
            continue;
        }
        
        if (ch >= 32) {
            int index = ch - 32;
            const uint8_t* glyph = font_basic[index];
            
            for (int r = 0; r < 8; r++) {
                uint8_t row = glyph[r];
                for (int bit = 0; bit < 8; bit++) {
                    if (row & (0x80 >> bit)) {
                        if (scale == 1) {
                            grace_fb_put_pixel(curr_x + bit, curr_y + r, c);
                        } else {
                            grace_fb_fill_rect(curr_x + bit * scale, curr_y + r * scale, scale, scale, c);
                        }
                    }
                }
            }
        }
        
        curr_x += 8 * scale;
    }
}

int MeasureText(const char* text, int fontSize)
{
    if (!text)
        return 0;

    int len = 0;
    while (text[len] != '\0') len++;

    int scale = fontSize / 8;
    if (scale < 1) scale = 1;
    return len * 8 * scale;
}

/* ============================
   Random Functions
   ============================ */

int GetRandomValue(int min, int max)
{
    static unsigned int seed = 12345;
    seed = seed * 1103515245 + 12345;
    return min + (seed % (max - min + 1));
}

/* ============================
   Utility Functions
   ============================ */

void WaitTime(double seconds)
{
    // Use kernel sleep syscall
    sleep_ms((int)(seconds * 1000));
}

int GetKeyPressed(void)
{
    if (raylib_state.key_queue_read < raylib_state.key_queue_count)
        return raylib_state.key_queue[raylib_state.key_queue_read++];

    return 0;
}

int GetCharPressed(void)
{
    if (raylib_state.char_queue_read < raylib_state.char_queue_count)
        return raylib_state.char_queue[raylib_state.char_queue_read++];

    return 0;
}

/* ============================
   Mouse Functions (Stubs)
   ============================ */

int IsMouseButtonPressed(int button)
{
    (void)button;
    return 0;
}

int IsMouseButtonDown(int button)
{
    (void)button;
    return 0;
}

int IsMouseButtonReleased(int button)
{
    (void)button;
    return 0;
}

int IsMouseButtonUp(int button)
{
    (void)button;
    return 1;
}

Vector2 GetMousePosition(void)
{
    Vector2 pos = {0, 0};
    return pos;
}

Vector2 GetMouseDelta(void)
{
    Vector2 delta = {0, 0};
    return delta;
}

float GetMouseWheelMove(void)
{
    return 0.0f;
}

Vector2 GetMouseWheelMoveV(void)
{
    Vector2 wheel = {0, 0};
    return wheel;
}

/* ============================
   Utility Functions
   ============================ */

void TakeScreenshot(const char* fileName)
{
    (void)fileName;
    // Not implemented
}

void SetConfigFlags(unsigned int flags)
{
    (void)flags;
}

void ShowCursor(void)
{
}

void HideCursor(void)
{
}

int IsCursorHidden(void)
{
    return 0;
}

void EnableCursor(void)
{
}

void DisableCursor(void)
{
}

/* ============================
   Vector Math Functions
   ============================ */

Vector2 Vector2Add(Vector2 v1, Vector2 v2)
{
    Vector2 result = { v1.x + v2.x, v1.y + v2.y };
    return result;
}

Vector2 Vector2Subtract(Vector2 v1, Vector2 v2)
{
    Vector2 result = { v1.x - v2.x, v1.y - v2.y };
    return result;
}

Vector2 Vector2Scale(Vector2 v, float scale)
{
    Vector2 result = { v.x * scale, v.y * scale };
    return result;
}

float Vector2Length(Vector2 v)
{
    return float_sqrt(v.x * v.x + v.y * v.y);
}

Vector2 Vector2Normalize(Vector2 v)
{
    float len = Vector2Length(v);
    if (len > 0) {
        return Vector2Scale(v, 1.0f / len);
    }
    return v;
}

/* ============================
   Predefined Colors
   ============================ */

Color LIGHTGRAY  = { 200, 200, 200, 255 };
Color GRAY       = { 130, 130, 130, 255 };
Color DARKGRAY   = { 80, 80, 80, 255 };
Color YELLOW     = { 255, 255, 0, 255 };
Color GOLD       = { 255, 215, 0, 255 };
Color ORANGE     = { 255, 200, 0, 255 };
Color PINK       = { 255, 175, 175, 255 };
Color RED        = { 255, 0, 0, 255 };
Color MAROON     = { 190, 0, 0, 255 };
Color GREEN      = { 0, 255, 0, 255 };
Color LIME       = { 0, 255, 0, 255 };
Color DARKGREEN  = { 0, 128, 0, 255 };
Color SKYBLUE    = { 102, 204, 255, 255 };
Color BLUE       = { 0, 0, 255, 255 };
Color DARKBLUE   = { 0, 0, 128, 255 };
Color PURPLE     = { 128, 0, 128, 255 };
Color VIOLET     = { 238, 130, 238, 255 };
Color DARKPURPLE = { 112, 31, 126, 255 };
Color BEIGE      = { 211, 176, 131, 255 };
Color BROWN      = { 165, 42, 42, 255 };
Color DARKBROWN  = { 100, 25, 25, 255 };
Color WHITE      = { 255, 255, 255, 255 };
Color BLACK      = { 0, 0, 0, 255 };
Color BLANK      = { 0, 0, 0, 0 };
Color MAGENTA    = { 255, 0, 255, 255 };
Color RAYWHITE   = { 245, 245, 245, 255 };

/* ============================
   Mouse Functions
   ============================ */

void SetMousePosition(int x, int y)
{
    (void)x;
    (void)y;
}

