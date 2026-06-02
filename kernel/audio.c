#include <kernel/audio.h>
#include <kernel/hda.h>
#include <kernel/io.h>
#include <kernel/timer.h>
#include <kernel/vga.h>
#include <kernel/memory.h>

#define PC_SPEAKER_PORT 0x61
#define PC_SPEAKER_CMD  0x63

static audio_stream_t g_audio_streams[8];
static int g_stream_count = 0;

/* Export for syscall access */
audio_stream_t* g_audio_streams_ptr = g_audio_streams;

void audio_init(void) {
    int hda_result = hda_init();
    if (hda_result < 0) {
        vga_puts("Audio: Using PC Speaker fallback\n");
    } else {
        vga_puts("Audio: HDA initialized\n");
    }
}

audio_stream_t* audio_open(int sample_rate, int channels, int bits_per_sample) {
    if (g_stream_count >= 8) return NULL;
    
    audio_stream_t* stream = &g_audio_streams[g_stream_count++];
    
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->bits_per_sample = bits_per_sample;
    stream->buffer_size = 4096; /* 4KB ring buffer */
    stream->buffer = kmalloc(stream->buffer_size);
    
    hda_init_buffer(stream->buffer_size);
    
    return stream;
}

int audio_write(audio_stream_t* stream, const void* data, usize size) {
    if (!stream || !data) return -1;
    
    if (stream->buffer) {
        /* Queue to HDA if available */
        hda_queue_audio(data, size < stream->buffer_size ? size : stream->buffer_size);
    }
    
    return (int)size;
}

void audio_close(audio_stream_t* stream) {
    if (!stream) return;
    
    hda_stop();
    
    if (stream->buffer) {
        kfree(stream->buffer);
        stream->buffer = NULL;
    }
}

void audio_beep(u32 freq, u32 duration_ms) {
    if (freq < AUDIO_FREQ_MIN || freq > AUDIO_FREQ_MAX) return;
    
    // PIT channel 2 for PC speaker
    u32 divisor = 1193180 / freq;
    
    // Send command to PIT
    outb(0x43, 0xB6); // Channel 2, square wave
    outb(0x42, (u8)(divisor & 0xFF));
    outb(0x42, (u8)((divisor >> 8) & 0xFF));
    
    // Enable speaker
    u8 val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val | 0x03);
    
    // Wait
    timer_sleep_ms(duration_ms);
    
    // Disable speaker
    val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val & 0xFC);
}

void audio_play_sample(u8* data, usize len) {
    (void)data; (void)len;
    // PCM playback not supported with PC speaker only
}

void audio_shutdown(void) {
    u8 val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val & 0xFC);
}