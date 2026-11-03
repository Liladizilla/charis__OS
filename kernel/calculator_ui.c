/* calculator_ui.c - Small native signed-integer calculator.
 *
 * The initial utility intentionally uses integer arithmetic only. Operations
 * are range-checked to signed 32-bit results so overflow cannot wrap silently.
 */
#include <kernel/calculator_ui.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/keyboard.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>

#define C_BG       CHARIS_UI_BACKGROUND
#define C_PANEL    CHARIS_UI_PANEL
#define C_RAISED   CHARIS_UI_PANEL_RAISED
#define C_BORDER   CHARIS_UI_BORDER
#define C_TEXT     CHARIS_UI_TEXT
#define C_MUTED    CHARIS_UI_MUTED
#define C_ACCENT   CHARIS_UI_ACCENT
#define C_SELECTED CHARIS_UI_ACCENT_SURFACE

static bool opened;
static char entry[12];
static u32 entry_length;
static s64 accumulator;
static bool has_accumulator;
static char pending_operator;
static bool replace_on_digit;
static bool error_state;
static const char* error_message;

static u32 px(u32 n) { return gfx_ui_scale(n); }
static u32 text_height(void) { return (u32)font_cell_h * gfx_text_get_scale(); }
static u32 max_u32(u32 a, u32 b) { return a > b ? a : b; }

static void draw_text(u32 x, u32 y, const char* s, u32 color) {
    gfx_draw_string(x, y, s, color, FB_TRANSPARENT);
}

static void set_entry_value(s64 value) {
    u64 magnitude;
    u32 pos = 0;
    if (value < -2147483648LL || value > 2147483647LL) {
        error_state = true;
        error_message = "OVERFLOW";
        entry[0] = 0;
        entry_length = 0;
        return;
    }
    if (value < 0) {
        entry[pos++] = '-';
        magnitude = (u64)(-value);
    } else {
        magnitude = (u64)value;
    }
    char digits[10];
    u32 count = 0;
    do {
        digits[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude && count < sizeof(digits));
    while (count) entry[pos++] = digits[--count];
    entry[pos] = 0;
    entry_length = pos;
    error_state = false;
    error_message = NULL;
}

static bool parse_entry(s64* value) {
    if (!value || !entry_length) return false;
    u32 i = 0;
    bool negative = false;
    if (entry[0] == '-') {
        negative = true;
        i++;
    }
    if (i == entry_length) return false;

    u64 parsed = 0;
    u64 limit = negative ? 2147483648ULL : 2147483647ULL;
    for (; i < entry_length; i++) {
        char ch = entry[i];
        if (ch < '0' || ch > '9') return false;
        u32 digit = (u32)(ch - '0');
        if (parsed > (limit - digit) / 10ULL) return false;
        parsed = parsed * 10ULL + digit;
    }
    *value = negative ? -(s64)parsed : (s64)parsed;
    return true;
}

static void clear_entry(void) {
    entry[0] = '0';
    entry[1] = 0;
    entry_length = 1;
    error_state = false;
    error_message = NULL;
}

static bool apply_operation(s64 left, s64 right, char op, s64* result) {
    if (!result) return false;
    s64 value = 0;
    switch (op) {
    case '+': value = left + right; break;
    case '-': value = left - right; break;
    case '*': value = left * right; break;
    case '/':
        if (!right) {
            error_state = true;
            error_message = "DIVIDE BY ZERO";
            return false;
        }
        value = left / right;
        break;
    default: return false;
    }
    if (value < -2147483648LL || value > 2147483647LL) {
        error_state = true;
        error_message = "OVERFLOW";
        return false;
    }
    *result = value;
    return true;
}

static void choose_operator(char op) {
    if (error_state) clear_entry();
    s64 current = 0;
    if (!parse_entry(&current)) current = 0;

    if (has_accumulator && pending_operator && !replace_on_digit) {
        s64 next;
        if (!apply_operation(accumulator, current, pending_operator, &next)) {
            has_accumulator = false;
            pending_operator = 0;
            return;
        }
        accumulator = next;
        set_entry_value(next);
    } else if (!has_accumulator || !replace_on_digit) {
        accumulator = current;
        has_accumulator = true;
    }

    pending_operator = op;
    replace_on_digit = true;
}

static void calculate_result(void) {
    if (error_state) return;
    if (!pending_operator || !has_accumulator) return;
    s64 right = 0, result = 0;
    if (!parse_entry(&right)) return;
    if (!apply_operation(accumulator, right, pending_operator, &result)) {
        has_accumulator = false;
        pending_operator = 0;
        replace_on_digit = true;
        return;
    }
    set_entry_value(result);
    has_accumulator = false;
    pending_operator = 0;
    replace_on_digit = true;
}

static void enter_digit(char digit) {
    if (digit < '0' || digit > '9') return;
    if (error_state || replace_on_digit) {
        clear_entry();
        replace_on_digit = false;
    }
    if (entry_length >= 10) return;
    if (entry_length == 1 && entry[0] == '0') {
        entry[0] = digit;
        entry[1] = 0;
        entry_length = 1;
        return;
    }
    entry[entry_length++] = digit;
    entry[entry_length] = 0;
}

static void backspace_entry(void) {
    if (error_state || replace_on_digit) {
        clear_entry();
        replace_on_digit = false;
        return;
    }
    if (entry_length > 1) entry[--entry_length] = 0;
    else clear_entry();
}

static void activate_key(char key) {
    if (key >= '0' && key <= '9') { enter_digit(key); return; }
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        choose_operator(key);
        return;
    }
    if (key == '=') { calculate_result(); return; }
    if (key == 'C') {
        clear_entry();
        accumulator = 0;
        has_accumulator = false;
        pending_operator = 0;
        replace_on_digit = false;
    }
}

void calculator_ui_open(void) {
    opened = true;
    clear_entry();
    accumulator = 0;
    has_accumulator = false;
    pending_operator = 0;
    replace_on_digit = false;
}

void calculator_ui_close(void) { opened = false; }
bool calculator_ui_is_open(void) { return opened; }

static void button(u32 x, u32 y, u32 w, u32 h, const char* label, bool accent) {
    fb_fill_rect(x, y, w, h, accent ? C_SELECTED : C_RAISED);
    fb_draw_rect(x, y, w, h, C_BORDER);
    u32 tw = 0, th = 0;
    gfx_text_size(label, &tw, &th);
    draw_text(x + (w > tw ? (w - tw) / 2 : 0),
              y + (h > th ? (h - th) / 2 : 0), label,
              accent ? C_ACCENT : C_TEXT);
}

static void geometry(u32* ox, u32* oy, u32* ow, u32* oh,
                     u32* opad, u32* ogap, u32* obutton_h,
                     u32* ogrid_y, u32* obutton_w) {
    u32 pad = px(16), gap = px(8);
    u32 title_h = max_u32(px(36), text_height() + px(8));
    u32 display_h = max_u32(px(58), text_height() + px(12));
    u32 button_h = max_u32(px(54), text_height() + px(10));
    u32 width = px(480);
    u32 min_width = text_height() * 4 + pad * 2 + gap * 3;
    if (width < min_width) width = min_width;
    u32 height = title_h + display_h + gap * 2 + button_h * 4 +
                 gap * 3 + pad * 2 + px(22);
    u32 x = g_framebuffer.width > width ? (g_framebuffer.width - width) / 2 : 0;
    u32 y = g_framebuffer.height > height ? (g_framebuffer.height - height) / 2 : 0;
    u32 button_w = (width - pad * 2 - gap * 3) / 4;
    if (ox) *ox = x;
    if (oy) *oy = y;
    if (ow) *ow = width;
    if (oh) *oh = height;
    if (opad) *opad = pad;
    if (ogap) *ogap = gap;
    if (obutton_h) *obutton_h = button_h;
    if (ogrid_y) *ogrid_y = y + title_h + display_h + gap * 2;
    if (obutton_w) *obutton_w = button_w;
}

void calculator_ui_render(void) {
    if (!opened || !g_framebuffer.initialized) return;
    u32 x, y, w, h, pad, gap, button_h, grid_y, button_w;
    geometry(&x, &y, &w, &h, &pad, &gap, &button_h, &grid_y, &button_w);
    fb_fill_rect(x + px(4), y + px(4), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, C_PANEL);
    fb_draw_rect(x, y, w, h, C_BORDER);

    u32 title_h = max_u32(px(36), text_height() + px(8));
    fb_fill_rect(x, y, w, title_h, C_RAISED);
    draw_text(x + pad, y + (title_h - text_height()) / 2,
              "Calculator", C_TEXT);
    u32 close_w = 0;
    gfx_text_size("X", &close_w, NULL);
    draw_text(x + w - pad - close_w, y + (title_h - text_height()) / 2,
              "X", C_MUTED);

    u32 display_h = max_u32(px(58), text_height() + px(12));
    u32 display_y = y + title_h + gap;
    fb_fill_rect(x + pad, display_y, w - pad * 2, display_h, C_BG);
    fb_draw_rect(x + pad, display_y, w - pad * 2, display_h, C_BORDER);

    const char* shown = error_state ? (error_message ? error_message : "ERROR") :
                        (entry_length ? entry : "0");
    u32 value_w = 0, value_h = 0;
    gfx_text_size(shown, &value_w, &value_h);
    u32 tx = x + w - pad - px(8) - value_w;
    u32 ty = display_y + (display_h > value_h ? (display_h - value_h) / 2 : 0);
    draw_text(tx, ty, shown, error_state ? 0xFFFF9D8Fu : C_TEXT);

    static const char* const keys[4][4] = {
        { "7", "8", "9", "/" },
        { "4", "5", "6", "*" },
        { "1", "2", "3", "-" },
        { "C", "0", "=", "+" }
    };
    for (u32 row = 0; row < 4; row++) {
        u32 by = grid_y + row * (button_h + gap);
        for (u32 col = 0; col < 4; col++) {
            u32 bx = x + pad + col * (button_w + gap);
            button(bx, by, button_w, button_h, keys[row][col],
                   (row == 3 && (col == 2 || col == 3)));
        }
    }
    draw_text(x + pad, y + h - text_height() - px(10),
              "Integer calculator. Esc closes, Enter calculates.", C_MUTED);
}

bool calculator_ui_handle_event(const input_event_t* event) {
    if (!opened || !event) return false;
    if (event->type == INPUT_KEY_UP) return true;
    if (event->type == INPUT_KEY_DOWN) {
        u8 code = (u8)event->key.scancode;
        if (code & 0x80) return true;
        if (code == KEY_ESCAPE) { calculator_ui_close(); return true; }
        if (code == KEY_ENTER) { calculate_result(); return true; }
        if (code == KEY_BACKSPACE) { backspace_entry(); return true; }
        if (code >= 0x02 && code <= 0x0A) {
            enter_digit((char)('1' + (code - 0x02)));
            return true;
        }
        if (code == 0x0B) { enter_digit('0'); return true; }
        if (code == 0x0C) { activate_key('-'); return true; }
        if (code == 0x0D) { activate_key('+'); return true; }
        if (code == 0x35) { activate_key('/'); return true; }
        if (code == 0x37) { activate_key('*'); return true; }
        if (code == 0x1C) { calculate_result(); return true; }
        if (code == 0x39) { enter_digit('0'); return true; }
        return true;
    }
    if (event->type != INPUT_MOUSE_DOWN) return true;

    u32 x, y, w, h, pad, gap, button_h, grid_y, button_w;
    geometry(&x, &y, &w, &h, &pad, &gap, &button_h, &grid_y, &button_w);
    int mx = event->mouse.x, my = event->mouse.y;
    if (mx < (int)x || mx >= (int)(x + w) ||
        my < (int)y || my >= (int)(y + h)) {
        calculator_ui_close();
        return true;
    }
    u32 title_h = max_u32(px(36), text_height() + px(8));
    u32 close_w = 0;
    gfx_text_size("X", &close_w, NULL);
    if (my >= (int)y && my < (int)(y + title_h) &&
        mx >= (int)(x + w - pad - close_w - px(8))) {
        calculator_ui_close();
        return true;
    }

    u32 relative_y = my >= (int)grid_y ? (u32)(my - (int)grid_y) : 0xFFFFFFFFU;
    u32 row_span = button_h + gap;
    if (relative_y == 0xFFFFFFFFU || row_span == 0) return true;
    u32 row = relative_y / row_span;
    if (row >= 4 || relative_y % row_span >= button_h) return true;
    u32 relative_x = mx >= (int)(x + pad) ? (u32)(mx - (int)(x + pad)) : 0xFFFFFFFFU;
    if (relative_x == 0xFFFFFFFFU) return true;
    u32 col_span = button_w + gap;
    u32 col = relative_x / col_span;
    if (col >= 4 || relative_x % col_span >= button_w) return true;

    static const char key_values[4][4] = {
        { '7', '8', '9', '/' },
        { '4', '5', '6', '*' },
        { '1', '2', '3', '-' },
        { 'C', '0', '=', '+' }
    };
    activate_key(key_values[row][col]);
    return true;
}
