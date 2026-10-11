/* Host-side tests for the graphical shell calculator's arithmetic core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/shell_ui.c"

framebuffer_t g_framebuffer;

usize kstrlen(const char* s) { return strlen(s); }
char* kstrcpy(char* dst, const char* src) { return strcpy(dst, src); }

static void expect(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}
static void reset_calculator(void) {
    calculator_open = false;
    calculator_press('C');
}
static void enter_digits(const char* digits) {
    for (usize i = 0; digits[i]; i++) calculator_press(digits[i]);
}
static void test_precedence_is_explicitly_sequential(void) {
    reset_calculator();
    enter_digits("12");
    calculator_press('+');
    enter_digits("3");
    calculator_press('=');
    expect(strcmp(calculator_display, "15") == 0,
           "addition produces the displayed result");
    calculator_press('*');
    enter_digits("2");
    calculator_press('=');
    expect(strcmp(calculator_display, "30") == 0,
           "a result can feed the next operation");
}
static void test_subtraction_can_produce_negative_values(void) {
    reset_calculator();
    enter_digits("3");
    calculator_press('-');
    enter_digits("8");
    calculator_press('=');
    expect(strcmp(calculator_display, "-5") == 0,
           "negative results render correctly");
}
static void test_division_by_zero_fails_closed(void) {
    reset_calculator();
    enter_digits("9");
    calculator_press('/');
    calculator_press('0');
    calculator_press('=');
    expect(calculator_error && strcmp(calculator_display, "Error") == 0,
           "division by zero displays an error");
    calculator_press('7');
    expect(!calculator_error && strcmp(calculator_display, "7") == 0,
           "typing a digit recovers from error state");
}
static void test_signed_32_bit_overflow_is_rejected(void) {
    reset_calculator();
    enter_digits("2147483647");
    calculator_press('+');
    calculator_press('1');
    calculator_press('=');
    expect(calculator_error && strcmp(calculator_display, "Error") == 0,
           "overflow does not wrap into a misleading result");
}
static void test_clear_resets_pending_operation(void) {
    reset_calculator();
    enter_digits("8");
    calculator_press('*');
    calculator_press('C');
    enter_digits("4");
    calculator_press('=');
    expect(strcmp(calculator_display, "4") == 0,
           "clear removes stale operator and accumulator");
}
int main(void) {
    test_precedence_is_explicitly_sequential();
    test_subtraction_can_produce_negative_values();
    test_division_by_zero_fails_closed();
    test_signed_32_bit_overflow_is_rejected();
    test_clear_resets_pending_operation();
    puts("Calculator arithmetic tests passed");
    return 0;
}
