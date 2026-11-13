#include <stdio.h>
#include <stdlib.h>
#include <kernel/fb.h>

static void expect(u32 actual, u32 expected, const char* message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%08x expected=%08x)\n",
                message, actual, expected);
        exit(1);
    }
}

int main(void) {
    expect(fb_blend_argb(0xFF123456u, 0xFFABCDEFu, 0),
           0xFF123456u, "zero coverage preserves the background");
    expect(fb_blend_argb(0xFF123456u, 0xFFABCDEFu, 255),
           0xFFABCDEFu, "full coverage uses the foreground");

    expect(fb_blend_argb(0xFF000000u, 0xFFFFFFFFu, 128),
           0xFF808080u, "half coverage blends white over black");

    expect(fb_blend_argb(0xFF0000FFu, 0xFFFF0000u, 128),
           0xFF80007Fu, "half coverage blends foreground and background channels");

    puts("framebuffer alpha blend tests: PASS");
    return 0;
}
