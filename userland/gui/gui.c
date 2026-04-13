// ============================
// GraceOS GUI Mode
// ============================

#include "gui.h"
#include "../raylib/raylib.h"
#include "../raygui/raygui.h"
#include "../../drivers/video/fb.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/input/mouse.h"
#include "../../kernel/log/klog.h"
#include "../../drivers/video/serial.h"

/* Simple 12x16 arrow cursor bitmap (1=black outline, 2=white fill) */
static const uint8_t cursor_bmp[16][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,1,1,0,0,0,0},
};

/* GUI state */
static struct {
    int screen_w;
    int screen_h;
    int running;
    int use_mouse;
} gui_state = {0};

static void gui_draw_cursor(Vector2 pos)
{
    int cx = (int)pos.x;
    int cy = (int)pos.y;
    
    /* Clip cursor to screen bounds */
    if (cx < 0 || cy < 0 || cx >= gui_state.screen_w - 12 || cy >= gui_state.screen_h - 16)
        return;
        
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 12; col++)
        {
            uint8_t px = cursor_bmp[row][col];
            if (px == 1)
                fb_put_pixel(cx + col, cy + row, 0xFF000000);  /* Black outline */
            else if (px == 2)
                fb_put_pixel(cx + col, cy + row, 0xFFFFFFFF);  /* White fill */
        }
}

/* Process keyboard events */
static void gui_process_keyboard(void)
{
    while (keyboard_haschar()) {
        unsigned char key = (unsigned char)keyboard_getchar();
        
        /* Keyboard cursor control (fallback when no mouse) */
        if (!gui_state.use_mouse) {
            Vector2 m = GetMousePosition();

            if (key == KEY_SPECIAL_UP) m.y -= 8;
            else if (key == KEY_SPECIAL_DOWN) m.y += 8;
            else if (key == KEY_SPECIAL_LEFT) m.x -= 8;
            else if (key == KEY_SPECIAL_RIGHT) m.x += 8;

            SetMousePosition((int)m.x, (int)m.y);
        }
        
        /* Global keyboard commands */
        if (key == 0x1B) {  /* ESC key */
            gui_state.running = 0;
        }
    }
}

/* Initialize GUI system */
static void gui_init(void)
{
    klog_init_msg("GUI: Starting framebuffer mode");
    serial_log("GUI: Initializing...");

    /* Step 1: Initialize window */
    InitWindow(0, 0, "GraceOS GUI");
    
    /* Step 2: Verify framebuffer is ready */
    if (!fb_ready()) {
        klog_error("GUI: Framebuffer not ready after InitWindow");
        serial_error("GUI: Framebuffer not ready");
        return;
    }

    struct fb_state* state = fb_get_state();
    gui_state.screen_w = (int)state->width;
    gui_state.screen_h = (int)state->height;
    
    serial_log("GUI: Framebuffer ready");
    serial_log_hex("Width", gui_state.screen_w);
    serial_log_hex("Height", gui_state.screen_h);
    
    /* Step 4: Check if mouse is available */
    gui_state.use_mouse = mouse_present();
    if (gui_state.use_mouse) {
        serial_log("GUI: Mouse detected and enabled");
        klog_log("GUI: Mouse detected\n");
    } else {
        serial_warn("GUI: No mouse detected - using keyboard cursor");
        klog_log("GUI: No mouse - using keyboard cursor\n");
    }

    /* Step 5: Direct write test (verify hardware mapping) */
    volatile uint32_t* hw_fb = (volatile uint32_t*)(uintptr_t)state->addr;
    for (int i = 0; i < 100; i++) {
        hw_fb[i] = 0xFFFF0000;  /* Red pixels at top-left */
    }
    serial_log("GUI: Direct write test done");

    /* Step 6: Test backbuffer + present */
    fb_clear(0xFF808080);  /* Clear grey screen */
    fb_present();
    serial_log("GUI: Grey screen test done");

    /* Brief pause to see grey screen */
    for (volatile int i = 0; i < 10000000; i++) {}

    if (WindowShouldClose()) {
        klog_error("GUI: Window closed immediately");
        serial_error("GUI: Window closed immediately");
        gui_state.running = 0;
        return;
    }
    
    gui_state.running = 1;
    serial_log("GUI: Initialization complete");
}

/* Main GUI loop */
void gui_run(void)
{
    /* Initialize GUI system */
    gui_init();
    
    if (!gui_state.running) {
        return;
    }

    /* Main event loop */
    while (!WindowShouldClose() && gui_state.running) {
        /* Process input devices */
        gui_process_keyboard();

        /* Begin drawing frame */
        BeginDrawing();
        
        /* Draw clear grey background */
        ClearBackground((Color){ 128, 128, 128, 255 });  /* Medium grey */
        
        /* Draw simple mouse cursor directly on grey background */
        Vector2 m = GetMousePosition();
        gui_draw_cursor(m);

        /* Present frame */
        EndDrawing();
    }

    serial_log("GUI: Shutting down");
    klog_log("GUI: Shutting down\n");
    
    /* Clean up */
    CloseWindow();
}

/* Force cursor position (useful for calibration) */
void gui_set_cursor(int x, int y)
{
    Vector2 m = GetMousePosition();

    if (m.x < 0) m.x = 0;
    if (m.y < 0) m.y = 0;
    if (m.x > gui_state.screen_w - 12)
        m.x = gui_state.screen_w - 12;
    if (m.y > gui_state.screen_h - 16)
        m.y = gui_state.screen_h - 16;

    SetMousePosition((int)m.x, (int)m.y);
}

/* Get current cursor position */
void gui_get_cursor(int* x, int* y)
{
    Vector2 m = GetMousePosition();

    if (x) *x = (int)m.x;
    if (y) *y = (int)m.y;
}

/* Check if GUI is using mouse */
int gui_has_mouse(void)
{
    return gui_state.use_mouse;
}