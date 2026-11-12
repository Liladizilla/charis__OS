/* terminal_ascii.h - Fixed-width CharisOS terminal identity mark. */
#pragma once

#include <kernel/types.h>

#define CHARISOS_ASCII_COLUMNS 19u
#define CHARISOS_ASCII_ROWS 12u

/* Every row is exactly 19 monospaced cells. The compact C mark remains
 * within the terminal's safe width at 300% text scale on the default window. */
static const char* const CHARISOS_ASCII_MARK[CHARISOS_ASCII_ROWS] = {
    "     ##########    ",
    "   ################",
    "  #####            ",
    " ####              ",
    "####               ",
    "####               ",
    "####               ",
    "####               ",
    " ####              ",
    "  #####            ",
    "   ################",
    "     ##########    "
};
