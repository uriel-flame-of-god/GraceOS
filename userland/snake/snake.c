#include "snake.h"
#include "../raylib/raylib.h"

#define GRID_COLS (GAME_WIDTH / GRID_SIZE)
#define GRID_ROWS (GAME_HEIGHT / GRID_SIZE)

typedef struct snake_seg {
	int x;
	int y;
} snake_seg_t;

typedef struct snake_game {
	snake_seg_t body[SNAKE_MAX_LENGTH];
	int length;
	int dir_x;
	int dir_y;
	int next_dir_x;
	int next_dir_y;
	int food_x;
	int food_y;
	int score;
	int game_over;
	int paused;
} snake_game_t;

static snake_game_t g_game;

static void int_to_text(int value, char* out)
{
	char digits[16];
	int i = 0;
	int n = value;

	if (n == 0)
	{
		out[0] = '0';
		out[1] = '\0';
		return;
	}

	if (n < 0)
	{
		out[0] = '-';
		out++;
		n = -n;
	}

	while (n > 0 && i < (int)sizeof(digits))
	{
		digits[i++] = (char)('0' + (n % 10));
		n /= 10;
	}

	for (int j = 0; j < i; j++)
		out[j] = digits[i - 1 - j];

	out[i] = '\0';
}

static void build_score_text(char* out, int score)
{
	char num[16];
	int_to_text(score, num);

	out[0] = 'S'; out[1] = 'c'; out[2] = 'o'; out[3] = 'r'; out[4] = 'e'; out[5] = ':'; out[6] = ' ';

	int index = 7;
	while (num[index - 7] != '\0' && index < 31)
	{
		out[index] = num[index - 7];
		index++;
	}

	out[index] = '\0';
}

static int snake_occupies(int x, int y)
{
	for (int i = 0; i < g_game.length; i++)
	{
		if (g_game.body[i].x == x && g_game.body[i].y == y)
			return 1;
	}

	return 0;
}

static void spawn_food(void)
{
	for (int attempt = 0; attempt < 256; attempt++)
	{
		int x = GetRandomValue(0, GRID_COLS - 1);
		int y = GetRandomValue(0, GRID_ROWS - 1);

		if (!snake_occupies(x, y))
		{
			g_game.food_x = x;
			g_game.food_y = y;
			return;
		}
	}

	for (int y = 0; y < GRID_ROWS; y++)
	{
		for (int x = 0; x < GRID_COLS; x++)
		{
			if (!snake_occupies(x, y))
			{
				g_game.food_x = x;
				g_game.food_y = y;
				return;
			}
		}
	}

	g_game.food_x = 0;
	g_game.food_y = 0;
}

static void reset_game(void)
{
	g_game.length = 4;
	g_game.score = 0;
	g_game.game_over = 0;
	g_game.paused = 0;

	g_game.dir_x = 1;
	g_game.dir_y = 0;
	g_game.next_dir_x = 1;
	g_game.next_dir_y = 0;

	int start_x = GRID_COLS / 2;
	int start_y = GRID_ROWS / 2;

	for (int i = 0; i < g_game.length; i++)
	{
		g_game.body[i].x = start_x - i;
		g_game.body[i].y = start_y;
	}

	spawn_food();
}

static void handle_input(void)
{
	if (IsKeyPressed(KEY_Q))
	{
		window_should_close = 1;
		return;
	}

	if (g_game.game_over)
	{
		if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
			reset_game();
		return;
	}

	if (IsKeyPressed(KEY_P))
		g_game.paused = !g_game.paused;

	if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
	{
		if (g_game.dir_y != 1)
		{
			g_game.next_dir_x = 0;
			g_game.next_dir_y = -1;
		}
	}
	else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
	{
		if (g_game.dir_y != -1)
		{
			g_game.next_dir_x = 0;
			g_game.next_dir_y = 1;
		}
	}
	else if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
	{
		if (g_game.dir_x != 1)
		{
			g_game.next_dir_x = -1;
			g_game.next_dir_y = 0;
		}
	}
	else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
	{
		if (g_game.dir_x != -1)
		{
			g_game.next_dir_x = 1;
			g_game.next_dir_y = 0;
		}
	}
}

static void update_game(void)
{
	if (g_game.game_over || g_game.paused)
		return;

	g_game.dir_x = g_game.next_dir_x;
	g_game.dir_y = g_game.next_dir_y;

	int next_x = g_game.body[0].x + g_game.dir_x;
	int next_y = g_game.body[0].y + g_game.dir_y;

	if (next_x < 0 || next_x >= GRID_COLS || next_y < 0 || next_y >= GRID_ROWS)
	{
		g_game.game_over = 1;
		return;
	}

	for (int i = 0; i < g_game.length; i++)
	{
		if (g_game.body[i].x == next_x && g_game.body[i].y == next_y)
		{
			g_game.game_over = 1;
			return;
		}
	}

	for (int i = g_game.length; i > 0; i--)
		g_game.body[i] = g_game.body[i - 1];

	g_game.body[0].x = next_x;
	g_game.body[0].y = next_y;

	if (next_x == g_game.food_x && next_y == g_game.food_y)
	{
		if (g_game.length < SNAKE_MAX_LENGTH - 1)
			g_game.length++;

		g_game.score += 10;
		spawn_food();
	}
}

static void draw_game(void)
{
	int screen_w = GetScreenWidth();
	int screen_h = GetScreenHeight();
	int board_w = GRID_COLS * GRID_SIZE;
	int board_h = GRID_ROWS * GRID_SIZE;
	int board_x = (screen_w - board_w) / 2;
	int board_y = (screen_h - board_h) / 2;

	if (board_y < 40)
		board_y = 40;

	BeginDrawing();
	ClearBackground((Color){14, 18, 24, 255});

	DrawRectangle(board_x - 2, board_y - 2, board_w + 4, board_h + 4, DARKGRAY);
	DrawRectangle(board_x, board_y, board_w, board_h, BLACK);
	DrawRectangleLines(board_x - 2, board_y - 2, board_w + 4, board_h + 4, LIGHTGRAY);

	for (int y = 0; y < GRID_ROWS; y++)
	{
		for (int x = 0; x < GRID_COLS; x++)
		{
			if (((x + y) & 1) == 0)
			{
				DrawRectangle(board_x + x * GRID_SIZE, board_y + y * GRID_SIZE,
							  GRID_SIZE, GRID_SIZE, (Color){20, 28, 20, 255});
			}
		}
	}

	DrawRectangle(board_x + g_game.food_x * GRID_SIZE + 2,
				  board_y + g_game.food_y * GRID_SIZE + 2,
				  GRID_SIZE - 4, GRID_SIZE - 4, RED);

	for (int i = g_game.length - 1; i >= 0; i--)
	{
		Color seg_color = (i == 0) ? LIME : GREEN;
		DrawRectangle(board_x + g_game.body[i].x * GRID_SIZE + 1,
					  board_y + g_game.body[i].y * GRID_SIZE + 1,
					  GRID_SIZE - 2, GRID_SIZE - 2, seg_color);
	}

	char score_text[32];
	build_score_text(score_text, g_game.score);
	DrawText("GRACEOS SNAKE", board_x, board_y - 30, 16, RAYWHITE);
	DrawText(score_text, board_x + 170, board_y - 30, 16, YELLOW);
	DrawText("Arrows/WASD move  P pause  Q quit", board_x, board_y + board_h + 10, 14, LIGHTGRAY);

	if (g_game.paused && !g_game.game_over)
	{
		const char* paused = "PAUSED";
		int w = MeasureText(paused, 26);
		DrawText(paused, board_x + (board_w - w) / 2, board_y + board_h / 2 - 20, 26, YELLOW);
	}

	if (g_game.game_over)
	{
		const char* game_over = "GAME OVER";
		const char* hint = "Press Enter to restart";
		int w1 = MeasureText(game_over, 30);
		int w2 = MeasureText(hint, 18);
		DrawText(game_over, board_x + (board_w - w1) / 2, board_y + board_h / 2 - 28, 30, RED);
		DrawText(hint, board_x + (board_w - w2) / 2, board_y + board_h / 2 + 10, 18, WHITE);
	}

	EndDrawing();
}

void snake_run(void)
{
	InitWindow(GAME_WIDTH, GAME_HEIGHT, "Snake");
	SetTargetFPS(60);

	reset_game();

	float move_timer = 0.0f;
	const float step_time = 0.11f;

	while (!WindowShouldClose())
	{
		handle_input();

		move_timer += GetFrameTime();
		while (move_timer >= step_time)
		{
			update_game();
			move_timer -= step_time;
		}

		draw_game();
	}

	CloseWindow();
}
