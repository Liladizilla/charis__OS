/* calculator_ui.c - Native, bounded integer calculator surface. */
#include <kernel/calculator_ui.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/keyboard.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>

#define CALC_MAX_INPUT_DIGITS 10u
#define CALC_MARGIN 16u
#define CALC_GAP 8u

static bool calculator_visible;
static bool calculator_shift;
static bool calculator_bounds_valid;
static u32 calculator_bounds_x, calculator_bounds_y;
static u32 calculator_bounds_w, calculator_bounds_h;

static char calculator_display[16] = "0";
static s64 calculator_accumulator;
static char calculator_operator;
static bool calculator_new_entry = true;
static bool calculator_error;

typedef struct {
    u32 x, y, w, h;
} calculator_rect_t;

typedef struct {
    calculator_rect_t body;
    calculator_rect_t display;
    calculator_rect_t keys[4][4];
    calculator_rect_t close;
} calculator_layout_t;

static u32 calc_px(u32 value) {
    u32 scaled = gfx_ui_scale(value);
    return scaled ? scaled : 1u;
}

static u32 calc_strlen(const char* text) {
    u32 length = 0;
    if (!text) return 0;
    while (text[length]) length++;
    return length;
}

static u32 calc_window_x(void) {
    return calculator_bounds_valid ? calculator_bounds_x : calc_px(24);
}

static u32 calc_window_y(void) {
    return calculator_bounds_valid ? calculator_bounds_y : calc_px(24);
}

static u32 calc_window_w(void) {
    if (calculator_bounds_valid) return calculator_bounds_w;
    u32 margin = calc_px(48);
    return g_framebuffer.width > margin ? g_framebuffer.width - margin
                                         : g_framebuffer.width;
}

static u32 calc_window_h(void) {
    if (calculator_bounds_valid) return calculator_bounds_h;
    u32 margin = calc_px(96);
    return g_framebuffer.height > margin ? g_framebuffer.height - margin
                                          : g_framebuffer.height;
}

u32 calculator_ui_titlebar_height(void) {
    return calc_px(44);
}

void calculator_ui_set_bounds(u32 x, u32 y, u32 width, u32 height) {
    if (!width || !height) {
        calculator_bounds_valid = false;
        return;
    }
    calculator_bounds_x = x;
    calculator_bounds_y = y;
    calculator_bounds_w = width;
    calculator_bounds_h = height;
    calculator_bounds_valid = true;
}

void calculator_ui_reset_bounds(void) {
    calculator_bounds_valid = false;
}

static void calculator_make_layout(calculator_layout_t* layout) {
    if (!layout) return;
    u32 x = calc_window_x();
    u32 y = calc_window_y();
    u32 w = calc_window_w();
    u32 h = calc_window_h();
    u32 title_h = calculator_ui_titlebar_height();
    u32 margin = calc_px(CALC_MARGIN);
    u32 gap = calc_px(CALC_GAP);

    layout->body = (calculator_rect_t){x, y, w, h};
    layout->close = (calculator_rect_t){
        x + (w > calc_px(42) ? w - calc_px(36) : 0),
        y + (title_h > calc_px(30) ? (title_h - calc_px(28)) / 2u : 0),
        calc_px(28), calc_px(28)
    };

    u32 display_y = y + title_h + margin;
    u32 display_h = calc_px(58);
    layout->display = (calculator_rect_t){
        x + margin, display_y,
        w > margin * 2u ? w - margin * 2u : 1u,
        display_h
    };

    u32 grid_x = x + margin;
    u32 grid_y = display_y + display_h + margin;
    u32 available_w = w > margin * 2u + gap * 3u
                    ? w - margin * 2u - gap * 3u : 4u;
    u32 available_h = h > (grid_y - y) + margin + gap * 3u
                    ? h - (grid_y - y) - margin - gap * 3u : 4u;
    u32 key_w = available_w / 4u;
    u32 key_h = available_h / 4u;
    if (!key_w) key_w = 1u;
    if (!key_h) key_h = 1u;

    for (u32 row = 0; row < 4u; row++) {
        for (u32 col = 0; col < 4u; col++) {
            layout->keys[row][col] = (calculator_rect_t){
                grid_x + col * (key_w + gap),
                grid_y + row * (key_h + gap),
                key_w, key_h
            };
        }
    }
}

static bool calculator_parse(s64* value) {
    if (!value || !calculator_display[0]) return false;
    u32 index = 0;
    bool negative = false;
    if (calculator_display[index] == '-') {
        negative = true;
        index++;
    }
    if (!calculator_display[index]) return false;

    u64 magnitude = 0;
    for (; calculator_display[index]; index++) {
        char ch = calculator_display[index];
        if (ch < '0' || ch > '9') return false;
        magnitude = magnitude * 10u + (u64)(ch - '0');
        if (magnitude > 2147483648ULL) return false;
    }

    if ((!negative && magnitude > 2147483647ULL) ||
        (negative && magnitude > 2147483648ULL))
        return false;
    *value = negative ? -(s64)magnitude : (s64)magnitude;
    return true;
}

static void calculator_set_value(s64 value) {
    char reverse[12];
    u32 length = 0;
    bool negative = value < 0;
    u64 magnitude = negative ? (u64)(-(value + 1)) + 1u : (u64)value;

    do {
        reverse[length++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude && length < sizeof(reverse));

    u32 out = 0;
    if (negative) calculator_display[out++] = '-';
    while (length) calculator_display[out++] = reverse[--length];
    calculator_display[out] = 0;
}

static bool calculator_apply(s64 left, char operation,
                             s64 right, s64* result) {
    if (!result) return false;
    s64 value;

    switch (operation) {
        case '+': value = left + right; break;
        case '-': value = left - right; break;
        case '*': value = left * right; break;
        case '/':
            if (!right) return false;
            value = left / right;
            break;
        default:
            return false;
    }

    if (value < (-2147483647LL - 1LL) || value > 2147483647LL)
        return false;
    *result = value;
    return true;
}

static void calculator_show_error(void) {
    calculator_display[0] = 'E';
    calculator_display[1] = 'r';
    calculator_display[2] = 'r';
    calculator_display[3] = 'o';
    calculator_display[4] = 'r';
    calculator_display[5] = 0;
    calculator_error = true;
    calculator_operator = 0;
    calculator_new_entry = true;
}

static void calculator_clear(void) {
    calculator_display[0] = '0';
    calculator_display[1] = 0;
    calculator_accumulator = 0;
    calculator_operator = 0;
    calculator_new_entry = true;
    calculator_error = false;
}

static void calculator_digit(char digit) {
    if (digit < '0' || digit > '9') return;
    if (calculator_error || calculator_new_entry) {
        calculator_display[0] = digit;
        calculator_display[1] = 0;
        calculator_error = false;
        calculator_new_entry = false;
        return;
    }

    u32 length = calc_strlen(calculator_display);
    if (length >= CALC_MAX_INPUT_DIGITS) return;
    if (length == 1u && calculator_display[0] == '0') {
        calculator_display[0] = digit;
        return;
    }
    if (length + 1u >= sizeof(calculator_display)) return;
    calculator_display[length] = digit;
    calculator_display[length + 1u] = 0;
}

static void calculator_backspace(void) {
    if (calculator_error) {
        calculator_clear();
        return;
    }
    if (calculator_new_entry && calculator_operator) return;

    u32 length = calc_strlen(calculator_display);
    if (length == 0u) {
        calculator_clear();
        return;
    }
    if ((calculator_display[0] == '-' && length <= 2u) || length == 1u) {
        calculator_display[0] = '0';
        calculator_display[1] = 0;
        calculator_new_entry = true;
        return;
    }
    calculator_display[length - 1u] = 0;
    calculator_new_entry = false;
}

static void calculator_press(char key) {
    if (key == 'C') {
        calculator_clear();
        return;
    }
    if (key >= '0' && key <= '9') {
        calculator_digit(key);
        return;
    }
    if (calculator_error) return;

    s64 current = 0;
    if (!calculator_parse(&current)) {
        calculator_show_error();
        return;
    }

    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (calculator_operator && !calculator_new_entry) {
            if (!calculator_apply(calculator_accumulator,
                                  calculator_operator, current, &current)) {
                calculator_show_error();
                return;
            }
            calculator_set_value(current);
        }
        calculator_accumulator = current;
        calculator_operator = key;
        calculator_new_entry = true;
        return;
    }

    if (key == '=') {
        if (!calculator_operator) {
            calculator_new_entry = true;
            return;
        }
        if (calculator_new_entry) current = calculator_accumulator;
        if (!calculator_apply(calculator_accumulator,
                              calculator_operator, current, &current)) {
            calculator_show_error();
            return;
        }
        calculator_set_value(current);
        calculator_accumulator = current;
        calculator_operator = 0;
        calculator_new_entry = true;
    }
}

void calculator_ui_open(void) {
    calculator_clear();
    calculator_shift = false;
    calculator_visible = true;
}

void calculator_ui_close(void) {
    calculator_visible = false;
    calculator_shift = false;
}

bool calculator_ui_is_open(void) {
    return calculator_visible;
}

bool calculator_ui_handle_key(u8 scancode, bool pressed) {
    if (!calculator_visible) return false;
    u8 code = scancode & 0x7Fu;

    if (code == 0x2Au || code == 0x36u) {
        calculator_shift = pressed;
        return true;
    }
    if (!pressed) return true;
    if (code == KEY_ESCAPE) {
        calculator_ui_close();
        return true;
    }
    if (code == KEY_ENTER) {
        calculator_press('=');
        return true;
    }
    if (code == KEY_BACKSPACE) {
        calculator_backspace();
        return true;
    }

    char key = calculator_shift
        ? keyboard_scancode_set1_shift[code]
        : keyboard_scancode_set1[code];
    if (key >= '0' && key <= '9') {
        calculator_press(key);
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        calculator_press(key);
    } else if (key == 'c' || key == 'C') {
        calculator_press('C');
    }
    return true;
}

bool calculator_ui_handle_mouse(int mx, int my) {
    if (!calculator_visible) return false;
    calculator_layout_t layout;
    calculator_make_layout(&layout);
    const calculator_rect_t* body = &layout.body;
    if (mx < (int)body->x || mx >= (int)(body->x + body->w) ||
        my < (int)body->y || my >= (int)(body->y + body->h))
        return false;

    const calculator_rect_t* close = &layout.close;
    if (mx >= (int)close->x && mx < (int)(close->x + close->w) &&
        my >= (int)close->y && my < (int)(close->y + close->h)) {
        calculator_ui_close();
        return true;
    }
    if (my < (int)(body->y + calculator_ui_titlebar_height()))
        return false; /* Leave unclaimed titlebar space to window dragging. */

    static const char keys[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };
    for (u32 row = 0; row < 4u; row++) {
        for (u32 col = 0; col < 4u; col++) {
            const calculator_rect_t* key = &layout.keys[row][col];
            if (mx >= (int)key->x && mx < (int)(key->x + key->w) &&
                my >= (int)key->y && my < (int)(key->y + key->h)) {
                calculator_press(keys[row][col]);
                return true;
            }
        }
    }
    return true;
}

static void calculator_draw_button(const calculator_rect_t* rect,
                                   const char* label, bool accent) {
    if (!rect || !label) return;
    u32 fill = accent ? CHARIS_UI_ACCENT_SURFACE : CHARIS_UI_SURFACE;
    fb_fill_rect(rect->x, rect->y, rect->w, rect->h, fill);
    fb_draw_rect(rect->x, rect->y, rect->w, rect->h,
                 accent ? CHARIS_UI_BORDER_STRONG : CHARIS_UI_BORDER);

    u32 text_w = 0, text_h = 0;
    gfx_text_size(label, &text_w, &text_h);
    u32 draw_x = rect->x + (rect->w > text_w ? (rect->w - text_w) / 2u : 2u);
    u32 draw_y = rect->y + (rect->h > text_h ? (rect->h - text_h) / 2u : 0u);
    gfx_draw_string(draw_x, draw_y, label, CHARIS_UI_TEXT, FB_TRANSPARENT);
}

void calculator_ui_render(void) {
    if (!calculator_visible || !g_framebuffer.initialized) return;

    calculator_layout_t layout;
    calculator_make_layout(&layout);
    const calculator_rect_t* body = &layout.body;
    if (body->w < calc_px(320) || body->h < calc_px(280)) return;

    fb_fill_rect(body->x + calc_px(5), body->y + calc_px(6),
                 body->w, body->h, CHARIS_UI_SHADOW);
    fb_fill_rect(body->x, body->y, body->w, body->h, CHARIS_UI_PANEL);
    fb_draw_rect(body->x, body->y, body->w, body->h, CHARIS_UI_BORDER_STRONG);

    u32 title_h = calculator_ui_titlebar_height();
    fb_fill_rect(body->x, body->y, body->w, title_h,
                 CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(body->x, body->y + title_h, body->w, calc_px(1),
                 CHARIS_UI_BORDER);
    gfx_draw_string(body->x + calc_px(18),
                    body->y + (title_h - calc_px(16)) / 2u,
                    "Calculator", CHARIS_UI_TEXT, FB_TRANSPARENT);

    fb_fill_rect(layout.close.x, layout.close.y,
                 layout.close.w, layout.close.h, CHARIS_UI_SURFACE);
    fb_draw_rect(layout.close.x, layout.close.y,
                 layout.close.w, layout.close.h, CHARIS_UI_BORDER);
    gfx_draw_string(layout.close.x + calc_px(10),
                    layout.close.y + calc_px(6),
                    "x", CHARIS_UI_TEXT, FB_TRANSPARENT);

    fb_fill_rect(layout.display.x, layout.display.y,
                 layout.display.w, layout.display.h, CHARIS_UI_BACKGROUND);
    fb_draw_rect(layout.display.x, layout.display.y,
                 layout.display.w, layout.display.h, CHARIS_UI_BORDER);
    gfx_draw_string(layout.display.x + calc_px(12),
                    layout.display.y + calc_px(7),
                    "SIGNED INTEGER", CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 value_w = 0, value_h = 0;
    gfx_text_size(calculator_display, &value_w, &value_h);
    u32 value_x = layout.display.x + layout.display.w -
                  (value_w + calc_px(12) < layout.display.w
                   ? value_w + calc_px(12) : layout.display.w - calc_px(4));
    gfx_draw_string(value_x,
                    layout.display.y + layout.display.h -
                    (value_h + calc_px(7) < layout.display.h
                     ? value_h + calc_px(7) : layout.display.h),
                    calculator_display,
                    calculator_error ? CHARIS_UI_WARNING : CHARIS_UI_TEXT,
                    FB_TRANSPARENT);

    static const char* const labels[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };
    for (u32 row = 0; row < 4u; row++) {
        for (u32 col = 0; col < 4u; col++) {
            calculator_draw_button(&layout.keys[row][col], labels[row][col],
                                   col == 3u || (row == 3u && col == 2u));
        }
    }
}
