/* logo.h - CharisOS startup identity */
#ifndef CHARISOS_LOGO_H
#define CHARISOS_LOGO_H

#include <kernel/types.h>

void logo_print(void);
void logo_set_stage(const char* stage);
void logo_draw_gfx(u32 y);
void logo_draw_gfx_centered(void);

#endif
