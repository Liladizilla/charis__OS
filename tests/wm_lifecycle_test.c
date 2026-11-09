/* Host-side lifecycle and bounds tests for the native window manager. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/wm.h>
#include <kernel/memory.h>
#include <kernel/fb.h>

/* Compile wm.c into this test translation unit. Unused rendering paths are
 * removed by --gc-sections; these tests exercise allocation and lifecycle. */
#include "../kernel/wm.c"

framebuffer_t g_framebuffer = {
    .width = 640,
    .height = 480,
    .initialized = true
};

static int allocations_before_failure = -1;
static unsigned allocations;
static unsigned frees;

void *kmalloc(usize size) {
    if (allocations_before_failure == 0) return NULL;
    if (allocations_before_failure > 0) allocations_before_failure--;
    void *p = malloc(size);
    if (p) allocations++;
    return p;
}
void kfree(void *ptr) {
    if (ptr) { frees++; free(ptr); }
}
void *kmemset(void *dst, u8 value, usize size) {
    return memset(dst, value, size);
}
char *kstrncpy(char *dst, const char *src, usize size) {
    if (!src) src = "";
    return strncpy(dst, src, size);
}

static void expect(int ok, const char *message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}
static void reset_manager(void) {
    while (wm_windows) wm_destroy_window(wm_windows);
    wm_windows = NULL;
    allocations_before_failure = -1;
}
static void test_rejects_invalid_and_oversized_dimensions(void) {
    reset_manager();
    expect(wm_create_window("zero", 0, 0, 0, 10) == NULL,
           "zero width rejected");
    expect(wm_create_window("negative", 0, 0, 10, -1) == NULL,
           "negative height rejected");
    expect(wm_create_window("huge", 0, 0, 4097, 10) == NULL,
           "dimension over hard limit rejected");
    expect(wm_create_window("buffer-cap", 0, 0, 2049, 2049) == NULL,
           "pixel buffer over 16 MiB rejected");
}
static void test_allocation_failure_is_clean(void) {
    reset_manager();
    unsigned before = frees;
    allocations_before_failure = 1; /* window object succeeds, pixel buffer fails */
    expect(wm_create_window("allocation failure", 0, 0, 64, 64) == NULL,
           "window creation reports buffer allocation failure");
    expect(wm_windows == NULL, "failed allocation does not publish a window");
    expect(frees == before + 1, "failed buffer allocation frees window object");
    allocations_before_failure = -1;
}
static void test_title_is_bounded_and_sanitized(void) {
    reset_manager();
    char long_title[256];
    memset(long_title, 'A', sizeof(long_title));
    long_title[sizeof(long_title)-1] = 0;
    long_title[3] = '\n';
    window_t *w = wm_create_window(long_title, 20, 20, 80, 60);
    expect(w != NULL, "window with long title created");
    expect(strlen(w->title) == WM_MAX_TITLE_LENGTH,
           "title is capped at documented maximum");
    expect(w->title[3] == ' ', "control byte replaced before drawing");
    wm_destroy_window(w);
}
static void test_focus_hit_testing_and_visibility(void) {
    reset_manager();
    window_t *back = wm_create_window("back", 40, 40, 160, 100);
    window_t *front = wm_create_window("front", 60, 60, 160, 100);
    expect(back && front, "overlapping windows created");
    expect(wm_get_focused() == front, "new window receives focus");
    expect(wm_get_window_at(70, 70) == front, "topmost window wins hit test");
    wm_focus_window(back);
    expect(wm_get_focused() == back, "explicit focus changes active window");
    expect(wm_get_window_at(70, 70) == back, "focused window is raised");
    front->visible = false;
    expect(wm_get_window_at(70, 70) == back, "hidden window is excluded from hit test");
    front->visible = true;
    front->minimized = true;
    expect(wm_get_window_at(70, 70) == back, "minimized window is excluded from hit test");
    expect(wm_get_focused() == back, "minimized window cannot remain active");
    wm_destroy_window(front);
    wm_destroy_window(back);
}
static void test_position_is_clamped_and_destroy_refocuses(void) {
    reset_manager();
    window_t *first = wm_create_window("first", 9000, 9000, 100, 100);
    expect(first != NULL, "window created for clamp test");
    expect(first->x == 540 && first->y == 352,
           "window origin clamped to visible framebuffer bounds");
    window_t *second = wm_create_window("second", 10, 10, 100, 100);
    expect(second != NULL && wm_get_focused() == second,
           "second window becomes focused");
    wm_destroy_window(second);
    expect(wm_get_focused() == first, "destroying focused window restores prior eligible focus");
    wm_destroy_window(first);
    expect(wm_windows == NULL && wm_get_focused() == NULL,
           "destroying final window leaves empty manager");
}
static void test_shell_surface_avoids_duplicate_pixel_buffer(void) {
    reset_manager();
    unsigned before = allocations;
    window_t *surface = wm_create_surface_window("Terminal", 20, 20, 400, 240);
    expect(surface != NULL, "shell surface is registered with window manager");
    expect(surface->buffer == NULL, "shell surface does not allocate a duplicate pixel buffer");
    expect(allocations == before + 1, "shell surface allocates only its metadata object");
    expect(wm_get_focused() == surface, "shell surface receives focus");
    expect(wm_get_window_at(30, 30) == surface, "shell surface participates in hit testing");
    wm_destroy_window(surface);
    expect(wm_get_focused() == NULL, "closing shell surface releases focus");
}

static void test_window_limit_and_repeated_reuse(void) {
    reset_manager();
    window_t *items[MAX_WINDOWS];
    for (unsigned i = 0; i < MAX_WINDOWS; i++) {
        items[i] = wm_create_window("limit", (int)i, (int)i, 32, 32);
        expect(items[i] != NULL, "window slot allocated below maximum");
    }
    expect(wm_create_window("overflow", 0, 0, 32, 32) == NULL,
           "window count cap enforced");
    for (unsigned i = 0; i < MAX_WINDOWS; i += 2) wm_destroy_window(items[i]);
    for (unsigned i = 0; i < MAX_WINDOWS / 2; i++)
        expect(wm_create_window("reuse", 0, 0, 32, 32) != NULL,
               "destroyed slot can be reused");
    expect(wm_create_window("overflow again", 0, 0, 32, 32) == NULL,
           "reused slots still respect maximum");
    reset_manager();
}

int main(void) {
    test_rejects_invalid_and_oversized_dimensions();
    test_allocation_failure_is_clean();
    test_title_is_bounded_and_sanitized();
    test_focus_hit_testing_and_visibility();
    test_position_is_clamped_and_destroy_refocuses();
    test_shell_surface_avoids_duplicate_pixel_buffer();
    test_window_limit_and_repeated_reuse();
    expect(allocations == frees, "all test window and buffer allocations released");
    puts("Window manager lifecycle and bounds tests passed");
    return 0;
}
