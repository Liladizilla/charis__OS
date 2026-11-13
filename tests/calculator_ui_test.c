/* Host regressions for the native window-managed calculator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/keyboard.h>

framebuffer_t g_framebuffer;

const char keyboard_scancode_set1[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3',
    [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x2E] = 'c', [0x35] = '/'
};
const char keyboard_scancode_set1_shift[128] = {
    [0x09] = '*', [0x0D] = '+'
};

u32 gfx_ui_scale(u32 value) { return value; }

#include "../kernel/calculator_ui.c"

static void expect(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static u8 scancode_for(char key) {
    for (u32 code = 0; code < 128u; code++) {
        if (keyboard_scancode_set1[code] == key) return (u8)code;
        if (keyboard_scancode_set1_shift[code] == key) return (u8)code;
    }
    fprintf(stderr, "No test scancode for '%c'\n", key);
    exit(1);
}

static void type_text(const char* text) {
    for (u32 i = 0; text[i]; i++) {
        char key = text[i];
        if (key == '+' || key == '*') {
            calculator_ui_handle_key(0x2Au, true);
            calculator_ui_handle_key(scancode_for(key), true);
            calculator_ui_handle_key(0x2Au, false);
        } else {
            calculator_ui_handle_key(scancode_for(key), true);
        }
    }
}

static void reset_calculator(void) {
    calculator_ui_open();
}

static void test_sequential_integer_arithmetic(void) {
    reset_calculator();
    type_text("12");
    type_text("+");
    type_text("3");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(strcmp(calculator_display, "15") == 0,
           "addition displays the expected result");

    type_text("*");
    type_text("2");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(strcmp(calculator_display, "30") == 0,
           "operator chaining evaluates sequentially from the prior result");
}

static void test_negative_results_and_truncating_division(void) {
    reset_calculator();
    type_text("3-8");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(strcmp(calculator_display, "-5") == 0,
           "subtraction supports signed negative results");

    type_text("/");
    type_text("2");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(strcmp(calculator_display, "-2") == 0,
           "integer division truncates toward zero");
}

static void test_division_by_zero_recovers_cleanly(void) {
    reset_calculator();
    type_text("9/0");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(calculator_error && strcmp(calculator_display, "Error") == 0,
           "division by zero displays an explicit error");

    type_text("7");
    expect(!calculator_error && strcmp(calculator_display, "7") == 0,
           "typing a new digit recovers from the error state");
}

static void test_signed_32_bit_overflow_is_rejected(void) {
    reset_calculator();
    type_text("2147483647+1");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(calculator_error && strcmp(calculator_display, "Error") == 0,
           "positive signed 32-bit overflow is rejected");

    reset_calculator();
    type_text("50000*50000");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(calculator_error && strcmp(calculator_display, "Error") == 0,
           "multiplication overflow is rejected without wrapping");
}

static void test_clear_and_backspace_state(void) {
    reset_calculator();
    type_text("8*");
    calculator_ui_handle_key(0x2Eu, true);
    type_text("4");
    calculator_ui_handle_key(KEY_ENTER, true);
    expect(strcmp(calculator_display, "4") == 0,
           "clear removes both the pending operator and accumulator");

    reset_calculator();
    type_text("3-8");
    calculator_ui_handle_key(KEY_ENTER, true);
    calculator_ui_handle_key(KEY_BACKSPACE, true);
    expect(strcmp(calculator_display, "0") == 0,
           "backspace safely clears a one-digit negative result");
}

static void test_window_geometry_and_mouse_hit_testing(void) {
    reset_calculator();
    calculator_ui_set_bounds(100u, 80u, 440u, 500u);
    calculator_layout_t layout;
    calculator_make_layout(&layout);
    expect(layout.body.x == 100u && layout.body.y == 80u &&
           layout.body.w == 440u && layout.body.h == 500u,
           "renderer layout uses exact window-manager bounds");

    calculator_rect_t seven = layout.keys[0][0];
    expect(calculator_ui_handle_mouse((int)(seven.x + seven.w / 2u),
                                      (int)(seven.y + seven.h / 2u)),
           "mouse click inside the seven key is consumed");
    expect(strcmp(calculator_display, "7") == 0,
           "mouse keypad uses the same arithmetic input path");

    expect(!calculator_ui_handle_mouse(20, 20),
           "click outside the calculator is left to the window manager");

    calculator_rect_t close = layout.close;
    expect(calculator_ui_handle_mouse((int)(close.x + 2u),
                                      (int)(close.y + 2u)),
           "calculator close control consumes its click");
    expect(!calculator_ui_is_open(),
           "close control hides the calculator application");
}

int main(void) {
    test_sequential_integer_arithmetic();
    test_negative_results_and_truncating_division();
    test_division_by_zero_recovers_cleanly();
    test_signed_32_bit_overflow_is_rejected();
    test_clear_and_backspace_state();
    test_window_geometry_and_mouse_hit_testing();
    puts("Native calculator UI tests passed");
    return 0;
}
