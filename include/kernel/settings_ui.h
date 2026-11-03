/* settings_ui.h - Native CharisOS system settings surface */
#pragma once
#include <kernel/types.h>
#include <kernel/input.h>

void settings_ui_open(void);
void settings_ui_close(void);
bool settings_ui_is_open(void);
void settings_ui_render(void);
bool settings_ui_handle_event(const input_event_t* event);
