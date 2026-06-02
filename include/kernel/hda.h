/* hda.h - Intel High Definition Audio driver */
#pragma once
#include <kernel/types.h>

#define HDA_MAX_CODECS  4
#define HDA_MAX_STREAMS 4

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u16 command_offset;
    u16 response_offset;
    u16 iso_offset;
} hda_registers_t;

typedef struct {
    u64 mmio_base;
    int codec_mask;
    int stream_mask;
    int sample_rate;
    int channels;
    int bits_per_sample;
    u32 buffer_addr;
    u32 buffer_size;
    int buffer_position;
} hda_state_t;

/* Initialize HDA controller */
int hda_init(void);

/* Setup audio stream for playback */
int hda_setup_stream(u32 physical_buffer_addr, u32 buffer_size, int sample_rate, int channels, int bits);

/* Queue audio data */
int hda_queue_audio(const void* data, usize size);

/* Stop audio */
void hda_stop(void);

/* Audio buffer management */
void hda_init_buffer(usize size);