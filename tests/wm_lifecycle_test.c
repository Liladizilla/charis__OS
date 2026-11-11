/* Host-side lifecycle, geometry and visibility tests for the native window manager. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/wm.h>
#include <kernel/memory.h>
#include <kernel/fb.h>
#include "../kernel/wm.c"

framebuffer_t g_framebuffer = { .width = 640, .height = 480, .initialized = true };
static u32 clip_reset_count, clip_set_count;
static u32 paint_order[8], paint_count;
void fb_reset_clip(void) { clip_reset_count++; }
void fb_put_pixel(u32 x, u32 y, u32 color) {
    (void)x; (void)y; (void)color;
}
void fb_set_clip(u32 x, u32 y, u32 w, u32 h) {
    (void)x; (void)y; (void)w; (void)h;
    clip_set_count++;
}
void graphics_set_color(u32 color) { (void)color; }
void graphics_rect(u32 x, u32 y, u32 w, u32 h, bool filled) {
    (void)x; (void)y; (void)w; (void)h; (void)filled;
}
void psf_draw_string(u32 x, u32 y, const char* str, u32 fg, u32 bg) {
    (void)x; (void)y; (void)str; (void)fg; (void)bg;
}
static void paint_recorder(window_t* win) {
    if (paint_count < sizeof(paint_order) / sizeof(paint_order[0]))
        paint_order[paint_count++] = win->z_order;
}
static int allocations_before_failure = -1;
static unsigned allocations, frees;
static u32 dispatched_events;
static window_t* dispatched_target;
static int dispatched_type;

static void event_recorder(window_t* win, int event_type, void* data) {
    (void)data;
    dispatched_events++;
    dispatched_target = win;
    dispatched_type = event_type;
}

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
    expect(w->x == 560 && w->y == 392, "move clamps titlebar and content to screen");
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
static void test_window_list_traversal_is_bounded_and_unique(void) {
    reset_manager();
    window_t *a = wm_create_surface_window("cycle-a", 0, 0, 100, 80);
    window_t *b = wm_create_surface_window("cycle-b", 0, 0, 100, 80);
    expect(a && b && b->next == a, "two-node list created in expected order");

    /* Deliberately corrupt the private link into a cycle. Public reads and
     * focus paths must stop at the manager's hard bound without duplicates. */
    a->next = b;
    expect(wm_get_window_count() == 2, "cyclic list counts unique nodes only");
    expect(wm_get_focused() == b, "focused lookup terminates on cyclic list");
    expect(wm_get_window_at(10, 10) == b, "hit testing terminates on cyclic list");
    expect(wm_focus_next() == a, "focus cycling terminates and wraps on cyclic list");
    expect(wm_get_focused() == a, "focus cycle selects the next unique node");
    wm_focus_window(b);
    wm_focus_window(a);
    expect(wm_get_focused() == a, "focus traversal terminates on cyclic list");
    expect(wm_minimize_window(a), "minimize uses bounded focus recovery");
    expect(wm_get_focused() == b, "focus recovery selects remaining visible node");
    wm_destroy_window((window_t*)(uintptr_t)1);
    wm_destroy_window(a);
    expect(wm_get_window_count() == 1, "destroying a real node reduces unique count");
    expect(wm_windows == b && b->next == NULL,
           "destroying from a cycle rebuilds a clean remaining chain");
    reset_manager();
    expect(wm_windows == NULL, "cycle test releases all remaining nodes");
}

static void test_surface_compositor_paints_visible_windows_in_z_order(void) {
    reset_manager();
    paint_count = 0;
    clip_reset_count = 0;
    clip_set_count = 0;
    window_t *back = wm_create_surface_window("paint-back", 10, 10, 120, 90);
    window_t *front = wm_create_surface_window("paint-front", 30, 30, 120, 90);
    window_t *hidden = wm_create_surface_window("paint-hidden", 50, 50, 120, 90);
    expect(back && front && hidden, "callback surfaces created");
    back->on_paint = paint_recorder;
    front->on_paint = paint_recorder;
    hidden->on_paint = paint_recorder;
    expect(wm_set_window_visible(hidden, false), "third surface hidden");
    wm_focus_window(back);
    wm_focus_window(front);
    wm_render_surfaces();
    expect(paint_count == 2, "compositor skips hidden surfaces");
    expect(paint_order[0] < paint_order[1], "compositor paints back-to-front by z-order");
    expect(clip_set_count == 2, "each surface is clipped before its callback");
    expect(clip_reset_count >= 3, "clip resets before and after surface callbacks");

    paint_count = 0;
    expect(wm_minimize_window(front), "front surface can be minimized");
    wm_render_surfaces();
    expect(paint_count == 1 && paint_order[0] == back->z_order,
           "compositor skips minimized surfaces");
    wm_destroy_window(hidden);
    wm_destroy_window(front);
    wm_destroy_window(back);
}

static void test_event_dispatch_targets_only_focused_window(void) {
    reset_manager();
    dispatched_events = 0;
    dispatched_target = NULL;
    dispatched_type = 0;
    window_t *back = wm_create_surface_window("event-back", 10, 10, 120, 80);
    window_t *front = wm_create_surface_window("event-front", 30, 30, 120, 80);
    expect(back && front, "event test windows created");
    back->on_event = event_recorder;
    front->on_event = event_recorder;

    wm_dispatch_event(71, NULL);
    expect(dispatched_events == 1 && dispatched_target == front &&
           dispatched_type == 71, "first event reaches only focused window");

    wm_focus_window(back);
    wm_dispatch_event(72, NULL);
    expect(dispatched_events == 2 && dispatched_target == back &&
           dispatched_type == 72, "focus change reroutes subsequent events");

    expect(wm_minimize_window(back), "focused event window minimizes");
    wm_dispatch_event(73, NULL);
    expect(dispatched_events == 3 && dispatched_target == front &&
           dispatched_type == 73, "minimized window no longer receives events");

    expect(wm_set_window_visible(front, false), "last visible event window hides");
    wm_dispatch_event(74, NULL);
    expect(dispatched_events == 3, "hidden windows receive no keyboard or pointer events");
    wm_destroy_window(front);
    wm_destroy_window(back);
}

static void test_variable_titlebar_geometry_is_consistent(void) {
    reset_manager();
    window_t *w = wm_create_surface_window("custom chrome", 10, 10, 120, 80);
    expect(w != NULL, "surface for custom titlebar created");
    expect(w->titlebar_height == WINDOW_TITLEBAR_HEIGHT, "default titlebar retained");
    expect(wm_set_titlebar_height(w, 44), "custom titlebar height accepted");
    expect(wm_get_window_at(20, 50) == w, "expanded titlebar is included in hit testing");
    expect(wm_move_window(w, 900, 900), "custom titlebar window can move");
    expect(w->y == 356, "custom titlebar geometry clamps to screen edge");
    expect(!wm_set_titlebar_height(w, 19), "titlebar below minimum rejected");
    expect(!wm_set_titlebar_height(w, 97), "titlebar above maximum rejected");
    wm_destroy_window(w);
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
    test_window_list_traversal_is_bounded_and_unique();
    test_surface_compositor_paints_visible_windows_in_z_order();
    test_event_dispatch_targets_only_focused_window();
    test_variable_titlebar_geometry_is_consistent();
    test_window_limit_and_repeated_reuse();
    expect(allocations == frees, "all test allocations released");
    puts("Window manager lifecycle, geometry and visibility tests passed");
    return 0;
}
