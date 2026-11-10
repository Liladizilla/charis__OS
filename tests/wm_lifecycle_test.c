/* Host-side lifecycle, geometry and visibility tests for the native window manager. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/wm.h>
#include <kernel/memory.h>
#include <kernel/fb.h>
#include "../kernel/wm.c"

framebuffer_t g_framebuffer = { .width = 640, .height = 480, .initialized = true };
static int allocations_before_failure = -1;
static unsigned allocations, frees;

void *kmalloc(usize size) {
    if (allocations_before_failure == 0) return NULL;
    if (allocations_before_failure > 0) allocations_before_failure--;
    void *p = malloc(size);
    if (p) allocations++;
    return p;
}
void kfree(void *ptr) { if (ptr) { frees++; free(ptr); } }
void *kmemset(void *dst, u8 value, usize size) { return memset(dst, value, size); }
char *kstrncpy(char *dst, const char *src, usize size) {
    if (!src) src = "";
    return strncpy(dst, src, size);
}
static void expect(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void reset_manager(void) {
    while (wm_windows) wm_destroy_window(wm_windows);
    wm_windows = NULL;
    allocations_before_failure = -1;
}
static void test_rejects_invalid_and_oversized_dimensions(void) {
    reset_manager();
    expect(wm_create_window("zero", 0, 0, 0, 10) == NULL, "zero width rejected");
    expect(wm_create_window("negative", 0, 0, 10, -1) == NULL, "negative height rejected");
    expect(wm_create_window("huge", 0, 0, 4097, 10) == NULL, "dimension over hard limit rejected");
    expect(wm_create_window("buffer-cap", 0, 0, 2049, 2049) == NULL, "pixel buffer over 16 MiB rejected");
}
static void test_allocation_failure_is_clean(void) {
    reset_manager();
    unsigned before = frees;
    allocations_before_failure = 1;
    expect(wm_create_window("allocation failure", 0, 0, 64, 64) == NULL, "buffer allocation failure reported");
    expect(wm_windows == NULL, "failed allocation does not publish a window");
    expect(frees == before + 1, "failed buffer allocation frees window object");
    allocations_before_failure = -1;
}
static void test_title_is_bounded_and_sanitized(void) {
    reset_manager();
    char long_title[256]; memset(long_title, 'A', sizeof(long_title));
    long_title[sizeof(long_title)-1] = 0; long_title[3] = '\n';
    window_t *w = wm_create_window(long_title, 20, 20, 80, 60);
    expect(w != NULL, "window with long title created");
    expect(strlen(w->title) == WM_MAX_TITLE_LENGTH, "title is capped");
    expect(w->title[3] == ' ', "control byte sanitized");
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
    expect(wm_set_window_visible(back, false), "focused window can be hidden");
    expect(wm_get_focused() == front, "hiding focused window restores another focus");
    expect(wm_get_window_at(70, 70) == front, "hidden window excluded from hit test");
    expect(wm_set_window_visible(back, true), "hidden window can be shown");
    front->minimized = true;
    expect(wm_get_window_at(70, 70) == back, "minimized window excluded from hit test");
    wm_destroy_window(front); wm_destroy_window(back);
}
static void test_position_is_clamped_and_destroy_refocuses(void) {
    reset_manager();
    window_t *first = wm_create_window("first", 9000, 9000, 100, 100);
    expect(first && first->x == 540 && first->y == 352, "window origin clamped");
    window_t *second = wm_create_window("second", 10, 10, 100, 100);
    expect(second && wm_get_focused() == second, "second window receives focus");
    wm_destroy_window(second);
    expect(wm_get_focused() == first, "destroy focused window restores prior focus");
    wm_destroy_window(first);
    expect(wm_windows == NULL && wm_get_focused() == NULL, "final destroy leaves empty manager");
}
static void test_shell_surface_avoids_duplicate_pixel_buffer(void) {
    reset_manager();
    unsigned before = allocations;
    window_t *surface = wm_create_surface_window("Terminal", 20, 20, 400, 240);
    expect(surface && surface->buffer == NULL, "shell surface has metadata only");
    expect(allocations == before + 1, "surface allocates only metadata");
    expect(wm_get_focused() == surface, "surface receives focus");
    expect(wm_get_window_at(30, 30) == surface, "surface participates in hit testing");
    unsigned before_resize = allocations;
    expect(wm_resize_window(surface, 500, 300), "bufferless surface geometry can be resized");
    expect(surface->buffer == NULL && allocations == before_resize,
           "resizing a shell surface does not allocate a content buffer");
    wm_destroy_window(surface);
    expect(wm_get_focused() == NULL, "closing surface releases focus");
}
static void test_geometry_operations_are_bounded(void) {
    reset_manager();
    window_t *w = wm_create_window("geometry", 10, 10, 80, 60);
    expect(w != NULL && wm_get_window_count() == 1, "window count reports live windows");
    expect(wm_move_window(w, 900, 900), "valid move accepted");
    expect(w->x == 540 && w->y == 392, "move clamps titlebar and content to screen");
    expect(!wm_move_window(NULL, 1, 1), "null move rejected");
    expect(!wm_resize_window(w, 2049, 2049), "oversized buffer resize rejected");
    expect(w->width == 80 && w->height == 60, "rejected resize leaves geometry unchanged");
    u32* old = w->buffer;
    old[0] = 0xFF123456u;
    expect(wm_resize_window(w, 100, 70), "valid resize accepted");
    expect(w->width == 100 && w->height == 70, "valid resize updates dimensions");
    expect(w->buffer[0] == 0xFF123456u, "resize preserves overlapping pixels");
    expect(wm_get_window_count() == 1, "resize does not alter window count");
    wm_destroy_window(w);
}
static void test_resize_allocation_failure_is_transactional(void) {
    reset_manager();
    window_t *w = wm_create_window("transaction", 0, 0, 32, 32);
    expect(w != NULL, "initial window allocated");
    u32 *old_buffer = w->buffer;
    allocations_before_failure = 0;
    expect(!wm_resize_window(w, 64, 64), "resize allocation failure reported");
    expect(w->buffer == old_buffer && w->width == 32 && w->height == 32,
           "failed resize preserves old buffer and geometry");
    allocations_before_failure = -1;
    wm_destroy_window(w);
}
static void test_minimize_restore_and_focus_cycle(void) {
    reset_manager();
    window_t *a = wm_create_surface_window("A", 0, 0, 100, 80);
    window_t *b = wm_create_surface_window("B", 10, 10, 100, 80);
    window_t *c = wm_create_surface_window("C", 20, 20, 100, 80);
    expect(a && b && c, "three metadata surfaces created");
    expect(wm_get_focused() == c, "last surface focused");
    expect(wm_focus_next() == a, "focus cycle wraps to oldest eligible window");
    expect(wm_focus_next() == b, "focus cycle advances by z-order");
    expect(wm_minimize_window(b), "focused surface minimizes");
    expect(wm_get_focused() == a, "minimize transfers focus to highest remaining z-order surface");
    expect(wm_get_window_at(30, 30) == a, "hit testing selects highest remaining z-order surface");
    expect(wm_restore_window(b), "minimized surface restores");
    expect(wm_get_focused() == b, "restored surface receives focus");
    expect(!wm_minimize_window(NULL), "null minimize rejected");
    wm_destroy_window(a); wm_destroy_window(b); wm_destroy_window(c);
}
static void test_window_limit_and_repeated_reuse(void) {
    reset_manager();
    window_t *items[MAX_WINDOWS];
    for (unsigned i = 0; i < MAX_WINDOWS; i++) {
        items[i] = wm_create_window("limit", (int)i, (int)i, 32, 32);
        expect(items[i] != NULL, "window slot allocated below maximum");
    }
    expect(wm_get_window_count() == MAX_WINDOWS, "window count reaches configured cap");
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
    test_geometry_operations_are_bounded();
    test_resize_allocation_failure_is_transactional();
    test_minimize_restore_and_focus_cycle();
    test_window_limit_and_repeated_reuse();
    expect(allocations == frees, "all test allocations released");
    puts("Window manager lifecycle, geometry and visibility tests passed");
    return 0;
}
