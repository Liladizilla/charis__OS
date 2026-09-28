/* logo.h - Boot banner */
#ifndef CHARISOS_LOGO_H
#define CHARISOS_LOGO_H

#include <kernel/types.h>

/* Print the CharisOS banner to the VGA text console. */
void logo_print(void);

/* Render the same banner onto the linear framebuffer, centred, at row y.
 * Does nothing if no framebuffer was provided by the firmware. */
void logo_draw_gfx(u32 y);

#endif
