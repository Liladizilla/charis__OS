/* terminal_ascii.h - Fixed-width CharisOS terminal identity mark. */
#pragma once

#include <kernel/types.h>

#define CHARISOS_ASCII_COLUMNS 13u
#define CHARISOS_ASCII_ROWS 6u

/* Every row is exactly 13 monospaced cells. This six-row C mark is designed
 * for the terminal's independent 8x16 font so it remains visible while the
 * desktop uses larger global text settings. */
static const char* const CHARISOS_ASCII_MARK[CHARISOS_ASCII_ROWS] = {
    "  ########## ",
    " ########### ",
    "##           ",
    "##           ",
    " ########### ",
    "  ########## "
};
