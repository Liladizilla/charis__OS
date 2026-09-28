/* shell_ui.h - Graphical shell (wallpaper, taskbar, start menu) */
#ifndef KERNEL_SHELL_UI_H
#define KERNEL_SHELL_UI_H

#include <kernel/types.h>

/* Entry point for the desktop task. Runs a render loop and never returns. */
void shell_ui_main(void* arg);

/* Route raw keyboard scancodes into the input queue. Call once, after
 * keyboard_init() and input_init(). */
void ui_install_input_hooks(void);

#endif
