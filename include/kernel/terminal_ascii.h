/* terminal_ascii.h - Fixed-width CharisOS terminal identity mark. */
#pragma once

#include <kernel/types.h>

#define CHARISOS_ASCII_COLUMNS 13u
#define CHARISOS_ASCII_ROWS 6u

/* A heavier six-row C mark, sized for the terminal's fixed-width glyphs. */
static const char* const CHARISOS_ASCII_MARK[CHARISOS_ASCII_ROWS] = {
    " ########### ",
    "##         ##",
    "##           ",
    "##           ",
    "##         ##",
    " ########### "
};
