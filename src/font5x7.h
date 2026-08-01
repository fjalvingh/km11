#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>
#include <avr/pgmspace.h>

#include "font.h"

// Classic 5x7 ASCII font, characters 0x20..0x7E, stored column major:
// five bytes per glyph, one byte per column, bit 0 is the top row.
// Bit 7 is always clear, which gives the blank eighth row of the cell.
//
// The table lives in flash (475 bytes) and is read with pgm_read_byte; there
// is no RAM copy of it anywhere.

#define FONT_FIRST_CHAR 0x20
#define FONT_LAST_CHAR  0x7E
#define FONT_WIDTH      5   // pixel columns actually drawn
#define FONT_HEIGHT     8   // pixel rows in a cell (7 used + 1 blank)
#define FONT_CELL_W     6   // FONT_WIDTH + one blank spacing column

extern const uint8_t Font5x7[] PROGMEM;

// Same table wrapped as a Font descriptor; this is what the drawing calls take.
// The taller alternative is FontLarge in font8x16.h.
// extern const Font FontSmall; -- declared in font.h

#endif // FONT5X7_H
