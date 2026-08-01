#ifndef FONT_H
#define FONT_H

#include <stdint.h>
#include <avr/pgmspace.h>

// Describes one bitmap font so the drawing code does not have to know which
// one it is working with. Glyph data is always column major with `bytesPerCol`
// bytes per column, least significant bit of the first byte being the top row;
// glyph n starts at (n - first) * width * bytesPerCol.
//
// The descriptor itself is a plain const struct: on this core (__AVR_ARCH__
// 103) .rodata is mapped into flash, so it costs no RAM and needs no
// pgm_read_ to get at it. The glyph table it points at is PROGMEM and is read
// with pgm_read_byte.
struct Font {
	const uint8_t *data;
	uint8_t first;			// first character present in the table
	uint8_t last;			// last character present
	uint8_t width;			// columns actually stored per glyph
	uint8_t cellW;			// width plus any spacing columns the drawer adds
	uint8_t height;			// pixel rows in a cell
	uint8_t bytesPerCol;	// 1 for up to 8 rows, 2 for up to 16
};

// The largest cellW any font here has; lcdDrawChar sizes its stack buffer with
// it. Raise it if a wider font is ever added.
#define FONT_MAX_CELL_W 8

extern const Font FontSmall;	// 5x7 in a 6x8 cell, see font5x7.h
extern const Font FontLarge;	// 8x16, see font8x16.h

#endif // FONT_H
