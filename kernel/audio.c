#include <kernel/audio.h>
#include <kernel/io.h>
#include <kernel/timer.h>
#include <kernel/vga.h>

#define PC_SPEAKER_PORT 0x61
#define PC_SPEAKER_CMD  0x63

void audio_init(void) {
    vga_puts("Audio: PC Speaker initialized\n");
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