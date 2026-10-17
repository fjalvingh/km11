#ifndef FONT8X14_H
#define FONT8X14_H

#include "font.h"

// 8x14 font, characters 0x20..0x7E: the 8x16 one with two rows less padding,
// so nine lines fit the 128 pixel height instead of eight. Use it through
// FontMedium:
//
//     lcdDrawText_P(0, 0, PSTR("BSR"), LCD_WHITE, LCD_BLACK, &FontMedium);
//
// The spacing between cells is part of the glyphs (column 7 is always blank),
// so FontMedium.cellW is 8, not 9.

#define FONT_MEDIUM_WIDTH   8
#define FONT_MEDIUM_HEIGHT  14
#define FONT_MEDIUM_CELL_W  8

#endif // FONT8X14_H
