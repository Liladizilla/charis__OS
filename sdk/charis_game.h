/* charis_game.h - Native Game SDK for CharisOS */
#pragma once
#include <kernel/types.h>

/* Game loop functions */
int charis_game_init(int width, int height, const char* title);
bool charis_game_running(void);
void charis_game_poll_events(void);
void charis_game_clear(u32 color);
void charis_game_flip(void);
void charis_game_sleep_fps(int target_fps);

/* Drawing functions */
void charis_game_draw_pixel(int x, int y, u32 color);
void charis_game_draw_rect(int x, int y, int w, int h, u32 color);
void charis_game_draw_tri(int x0, int y0, int x1, int y1, int x2, int y2, u32 color);
void charis_game_draw_mesh(int vertex_count, const void* vertices, int tri_count, const void* tris);

/* Audio functions */
int charis_game_audio_open(int sample_rate, int channels, int bits);
int charis_game_audio_play(int stream_id, const void* pcm, usize size);
void charis_game_audio_close(int stream_id);

/* Input functions */
int charis_game_get_mouse_x(void);
int charis_game_get_mouse_y(void);
bool charis_game_get_mouse_button(int button); /* 0=left, 1=right, 2=middle */
bool charis_game_is_key_pressed(int keycode);

/* Time */
u64 charis_game_get_ticks(void);