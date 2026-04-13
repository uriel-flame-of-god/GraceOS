// ============================
// GraceOS Raylib Compatibility Header
// ============================

#ifndef GRACEOS_RAYLIB_H
#define GRACEOS_RAYLIB_H

#include "../../lib/libc/int.h"

/* ============================
   Basic Types
   ============================ */

typedef struct Color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} Color;

typedef struct Vector2 {
    float x;
    float y;
} Vector2;

typedef struct Vector3 {
    float x;
    float y;
    float z;
} Vector3;

typedef struct Rectangle {
    float x;
    float y;
    float width;
    float height;
} Rectangle;

/* ============================
   Predefined Colors
   ============================ */

extern Color LIGHTGRAY;
extern Color GRAY;
extern Color DARKGRAY;
extern Color YELLOW;
extern Color GOLD;
extern Color ORANGE;
extern Color PINK;
extern Color RED;
extern Color MAROON;
extern Color GREEN;
extern Color LIME;
extern Color DARKGREEN;
extern Color SKYBLUE;
extern Color BLUE;
extern Color DARKBLUE;
extern Color PURPLE;
extern Color VIOLET;
extern Color DARKPURPLE;
extern Color BEIGE;
extern Color BROWN;
extern Color DARKBROWN;
extern Color WHITE;
extern Color BLACK;
extern Color BLANK;
extern Color MAGENTA;
extern Color RAYWHITE;

/* ============================
   Key Codes (ASCII + Extensions)
   ============================ */

#define KEY_NULL            0
#define KEY_APOSTROPHE      39
#define KEY_COMMA           44
#define KEY_MINUS           45
#define KEY_PERIOD          46
#define KEY_SLASH           47
#define KEY_ZERO            48
#define KEY_ONE             49
#define KEY_TWO             50
#define KEY_THREE           51
#define KEY_FOUR            52
#define KEY_FIVE            53
#define KEY_SIX             54
#define KEY_SEVEN           55
#define KEY_EIGHT           56
#define KEY_NINE            57
#define KEY_SEMICOLON       59
#define KEY_EQUAL           61
#define KEY_A               65
#define KEY_B               66
#define KEY_C               67
#define KEY_D               68
#define KEY_E               69
#define KEY_F               70
#define KEY_G               71
#define KEY_H               72
#define KEY_I               73
#define KEY_J               74
#define KEY_K               75
#define KEY_L               76
#define KEY_M               77
#define KEY_N               78
#define KEY_O               79
#define KEY_P               80
#define KEY_Q               81
#define KEY_R               82
#define KEY_S               83
#define KEY_T               84
#define KEY_U               85
#define KEY_V               86
#define KEY_W               87
#define KEY_X               88
#define KEY_Y               89
#define KEY_Z               90
#define KEY_LEFT_BRACKET    91
#define KEY_BACKSLASH       92
#define KEY_RIGHT_BRACKET   93
#define KEY_GRAVE           96
#define KEY_SPACE           32
#define KEY_ESCAPE          256
#define KEY_ENTER           257
#define KEY_TAB             258
#define KEY_BACKSPACE       259
#define KEY_INSERT          260
#define KEY_DELETE          261
#define KEY_RIGHT           262
#define KEY_LEFT            263
#define KEY_DOWN            264
#define KEY_UP              265
#define KEY_PAGE_UP         266
#define KEY_PAGE_DOWN       267
#define KEY_HOME            268
#define KEY_END             269
#define KEY_CAPS_LOCK       280
#define KEY_SCROLL_LOCK     281
#define KEY_NUM_LOCK        282
#define KEY_PRINT_SCREEN    283
#define KEY_PAUSE           284
#define KEY_F1              290
#define KEY_F2              291
#define KEY_F3              292
#define KEY_F4              293
#define KEY_F5              294
#define KEY_F6              295
#define KEY_F7              296
#define KEY_F8              297
#define KEY_F9              298
#define KEY_F10             299
#define KEY_F11             300
#define KEY_F12             301
#define KEY_LEFT_SHIFT      340
#define KEY_LEFT_CONTROL    341
#define KEY_LEFT_ALT        342
#define KEY_LEFT_SUPER      343
#define KEY_RIGHT_SHIFT     344
#define KEY_RIGHT_CONTROL   345
#define KEY_RIGHT_ALT       346
#define KEY_RIGHT_SUPER     347
#define KEY_KB_MENU         348

/* ============================
   Window Functions
   ============================ */

/* Global window state flag - can be set by user code to request window close */
extern int window_should_close;

void InitWindow(int width, int height, const char* title);
int WindowShouldClose(void);
void CloseWindow(void);
int IsWindowReady(void);
int GetScreenWidth(void);
int GetScreenHeight(void);

/* ============================
   Drawing Functions
   ============================ */

void ClearBackground(Color color);
void BeginDrawing(void);
void EndDrawing(void);
void DrawColor(Color color);
void DrawPixel(int x, int y, Color color);
void DrawLine(int startPosX, int startPosY, int endPosX, int endPosY, Color color);
void DrawCircle(int centerX, int centerY, float radius, Color color);
void DrawRectangle(int posX, int posY, int width, int height, Color color);
void DrawRectangleLines(int posX, int posY, int width, int height, Color color);
void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color);
void DrawTriangleFilled(Vector2 v1, Vector2 v2, Vector2 v3, Color color);

/* ============================
   Text Functions
   ============================ */

void DrawText(const char* text, int posX, int posY, int fontSize, Color color);
int MeasureText(const char* text, int fontSize);

/* ============================
   Input Functions
   ============================ */

int IsKeyPressed(int key);
int IsKeyDown(int key);
int IsKeyReleased(int key);
int IsKeyUp(int key);
int GetKeyPressed(void);
int GetCharPressed(void);

int IsMouseButtonPressed(int button);
int IsMouseButtonDown(int button);
Vector2 GetMousePosition(void);
Vector2 GetMouseDelta(void);

/* ============================
   Timing Functions
   ============================ */

void SetTargetFPS(int fps);
int GetFPS(void);
float GetFrameTime(void);
double GetTime(void);
void WaitTime(double seconds);

/* ============================
   Utility Functions
   ============================ */

void TakeScreenshot(const char* fileName);
int GetRandomValue(int min, int max);
void SetConfigFlags(unsigned int flags);
void ShowCursor(void);
void HideCursor(void);
int IsCursorHidden(void);
void EnableCursor(void);
void DisableCursor(void);

/* ============================
   Vector Math
   ============================ */

Vector2 Vector2Add(Vector2 v1, Vector2 v2);
Vector2 Vector2Subtract(Vector2 v1, Vector2 v2);
Vector2 Vector2Scale(Vector2 v, float scale);
float Vector2Length(Vector2 v);
Vector2 Vector2Normalize(Vector2 v);

/* ============================
   Mouse Buttons
   ============================ */

#define MOUSE_BUTTON_LEFT   0
#define MOUSE_BUTTON_RIGHT  1
#define MOUSE_BUTTON_MIDDLE 2
#define MOUSE_BUTTON_SIDE   3
#define MOUSE_BUTTON_EXTRA  4
#define MOUSE_BUTTON_FORWARD 5
#define MOUSE_BUTTON_BACK   6

/* ============================
   Mouse Functions
   ============================ */

void SetMousePosition(int x, int y);
Vector2 GetMouseDelta(void);
int IsMouseButtonPressed(int button);
int IsMouseButtonDown(int button);
int IsMouseButtonReleased(int button);
int IsMouseButtonUp(int button);
float GetMouseWheelMove(void);
Vector2 GetMouseWheelMoveV(void);

#endif /* GRACEOS_RAYLIB_H */