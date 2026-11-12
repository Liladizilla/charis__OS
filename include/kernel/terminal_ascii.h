/* terminal_ascii.h - Fixed-width CharisOS terminal identity mark. */
#pragma once

#include <kernel/types.h>

#define CHARISOS_ASCII_COLUMNS 13u
#define CHARISOS_ASCII_ROWS 6u

/* Every row is exactly 13 cells. Drawn at an explicit 2x glyph scale,
 * the mark remains prominent without consuming the terminal transcript. */
static const char* const CHARISOS_ASCII_MARK[CHARISOS_ASCII_ROWS] = {
    "   ##########",
    " ########### ",
    "####         ",
    "####         ",
    " ########### ",
    "   ##########"
};
