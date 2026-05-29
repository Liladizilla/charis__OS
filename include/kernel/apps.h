/* apps.h - Built-in applications for CharisOS */
#pragma once
#include <kernel/types.h>

// Application entry points
void app_terminal_main(void);
void app_filemanager_main(void);
void app_texteditor_main(void);
void app_calculator_main(void);
void app_settings_main(void);

// Register all built-in apps
void apps_init(void);