#ifndef FONT8X16_H
#define FONT8X16_H

#include "font.h"

// 8x16 font, characters 0x20..0x7E, twice the height and rather more than
// twice the legibility of the 5x7 one. Use it through FontLarge:
//
//     lcdDrawText_P(0, 0, PSTR("BSR"), LCD_WHITE, LCD_BLACK, &FontLarge);
//
// The spacing between cells is part of the glyphs (column 7 and row 15 are
// always blank), so FontLarge.cellW is 8, not 9.

#define FONT_LARGE_WIDTH   8
#define FONT_LARGE_HEIGHT  16
#define FONT_LARGE_CELL_W  8

#endif // FONT8X16_H
