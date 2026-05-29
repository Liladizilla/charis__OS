/* audio.h - PC Speaker / Audio driver for CharisOS */
#pragma once
#include <kernel/types.h>

#define AUDIO_FREQ_MIN    20
#define AUDIO_FREQ_MAX    20000

void audio_init(void);
void audio_beep(u32 freq, u32 duration_ms);
void audio_play_sample(u8* data, usize len);
void audio_shutdown(void);