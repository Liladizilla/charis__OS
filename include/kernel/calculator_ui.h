/* calculator_ui.h - Native desktop calculator utility */
#pragma once
#include <kernel/types.h>
#include <kernel/input.h>

void calculator_ui_open(void);
void calculator_ui_close(void);
bool calculator_ui_is_open(void);
void calculator_ui_render(void);
bool calculator_ui_handle_event(const input_event_t* event);
