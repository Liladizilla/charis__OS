/* audio.h - High-level audio subsystem */
#pragma once
#include <kernel/types.h>

typedef enum {
    AUDIO_FORMAT_U8,
    AUDIO_FORMAT_S16_LE,
    AUDIO_FORMAT_S16_BE
} audio_format_t;

#define AUDIO_FREQ_MIN  20
#define AUDIO_FREQ_MAX  20000

typedef struct {
    int sample_rate;
    int channels;
    int bits_per_sample;
    usize buffer_size;
    void* buffer;
} audio_stream_t;

/* Initialize audio subsystem */
int audio_init(void);

/* Open an audio stream for playback */
audio_stream_t* audio_open(int sample_rate, int channels, int bits_per_sample);

/* Write audio data to stream */
int audio_write(audio_stream_t* stream, const void* data, usize size);

/* Close audio stream */
void audio_close(audio_stream_t* stream);

/* Beep for legacy compatibility */
void audio_beep(u32 freq, u32 duration_ms);