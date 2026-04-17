#ifndef SNAKE_H
#define SNAKE_H

#include "../../lib/libc/int.h"

// Game Constants
#define SNAKE_MAX_LENGTH 256
#define GRID_SIZE 20
#define GAME_WIDTH 640
#define GAME_HEIGHT 360
#define GAME_TARGET_FPS 60
#define GAME_FRAME_MS (1000 / GAME_TARGET_FPS)  /* 16.67ms per frame */

/* Game state structure for non-blocking operation */
typedef struct {
    int initialized;
    int running;
    uint64_t last_frame_time;
    float accumulator;
} snake_state_t;

/* Non-blocking game functions */
void snake_init(void);
int snake_update(float dt);
void snake_render(void);
void snake_shutdown(void);

/* Legacy blocking function (calls non-blocking variants in loop) */
void snake_run(void);

#endif // SNAKE_H
