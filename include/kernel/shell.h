/* shell.h - Simple text shell */
#pragma once

#include <kernel/types.h>

void shell_init(void);
void shell_main(void* arg);

/* Run a single command line. Returns true if it was recognised. Exposed so the
 * graphical start menu can launch the same commands the prompt does instead of
 * duplicating them. */
bool shell_execute(char* line);

