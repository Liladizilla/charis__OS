/* terminal_ascii.h - Fixed-width CharisOS terminal identity mark. */
#pragma once

#include <kernel/types.h>

#define CHARISOS_ASCII_COLUMNS 13u
#define CHARISOS_ASCII_ROWS 7u

/* A compact C mark. Each row contains exactly thirteen fixed cells. */
static const char* const CHARISOS_ASCII_MARK[CHARISOS_ASCII_ROWS] = {
    "   #######   ",
    " ########### ",
    "##         ##",
    "##           ",
    "##           ",
    " ########### ",
    "   #######   "
};
