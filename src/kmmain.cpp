#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

#include "st7789.h"
#include "font5x7.h"

// The 20MHz fuse only selects the oscillator; the main clock still comes out
// of a divide-by-6 prescaler after reset. Turning that off is what actually
// makes F_CPU (and therefore _delay_ms and the SPI clock) match the Makefile.
static void clockInit() {
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0);
}

// One text line is FONT_HEIGHT pixels; leave one pixel of leading between them.
#define LINE_H (FONT_HEIGHT + 1)
static inline uint16_t lineY(uint8_t line) {
	return (uint16_t) line * LINE_H;
}

// Paints a test pattern: a title, some coloured signal-ish text, and the whole
// printable character set laid out the way the signal display will use it.
static void drawTestScreen() {
	lcdFill(LCD_BLACK);

	lcdDrawText_P(0, lineY(0), PSTR("KM11 11/05 MAINT"), LCD_YELLOW, LCD_BLACK);

	lcdDrawText_P(0, lineY(1), PSTR("BSR"), LCD_GREY, LCD_BLACK);
	lcdDrawText_P(4 * FONT_CELL_W, lineY(1), PSTR("0000 1111"), LCD_GREEN, LCD_BLACK);
	lcdDrawText_P(0, lineY(2), PSTR("ISR"), LCD_GREY, LCD_BLACK);
	lcdDrawText_P(4 * FONT_CELL_W, lineY(2), PSTR("1010 0101"), LCD_RED, LCD_BLACK);

	// Every printable character, wrapped to the screen width. This is the part
	// that shows the font table and the glyph writer agree.
	uint8_t line = 4;
	uint16_t x = 0;
	for(uint8_t c = FONT_FIRST_CHAR; c <= FONT_LAST_CHAR; c++) {
		if(x + FONT_CELL_W > LCD_W) {
			x = 0;
			line++;
			if(lineY(line) + FONT_HEIGHT > LCD_H)
				break;
		}
		x = lcdDrawChar(x, lineY(line), (char) c, LCD_WHITE, LCD_BLACK);
	}
}

int main() {
	clockInit();
	lcdInit();
	drawTestScreen();

	for(;;) {
	}
}
