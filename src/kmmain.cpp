#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

#include "st7789.h"
#include "font5x7.h"

// The 20MHz fuse only selects the oscillator; the main clock still comes out of
// a divide-by-6 prescaler after reset, so the prescaler has to be set here for
// F_CPU (and with it _delay_ms and the SPI clock) to mean anything.
//
// It is set to divide by two, NOT switched off: the speed grade of this part is
// 20MHz only from 4.5V up. At the 3.3V this board runs on, 10MHz is the
// ceiling, and 20MHz would be out of spec with no promise that anything works.
// Dividing the 20MHz oscillator by two lands exactly on that ceiling. If fuse2
// is left at the 16MHz default this gives 8MHz instead, which is also fine -
// only the timings scale.
static void clockInit() {
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PDIV_2X_gc | CLKCTRL_PEN_bm);
}

// One text line is FONT_HEIGHT pixels; leave one pixel of leading between them.
#define LINE_H (FONT_HEIGHT + 1)
static inline uint16_t lineY(uint8_t line) {
	return (uint16_t) line * LINE_H;
}

// Unsigned decimal into a caller supplied buffer, right aligned in `width` and
// space padded. Small enough to be worth having instead of pulling in printf,
// which would cost well over a kilobyte of flash.
static void formatNumber(char *buf, uint16_t value, uint8_t width) {
	buf[width] = '\0';
	for(uint8_t i = width; i-- > 0;) {
		buf[i] = (char) ('0' + (value % 10));
		value /= 10;
		if(value == 0 && i > 0) {
			while(i-- > 0)
				buf[i] = ' ';
			break;
		}
	}
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

// Wiring test. No SPI, no protocol: SPI0 is switched off and all five lines to
// the display are toggled by hand, each at its own frequency, so any one of
// them can be identified on a slow scope or a multimeter without triggering on
// anything. Probe at the *display end* of the ribbon, not at the MCU:
//
//   CS   (PA4, J2.10)  8 Hz
//   MOSI (PA1, J2.4)   4 Hz
//   SCK  (PA3, J2.8)   2 Hz
//   DC   (PB2, J2.12)  1 Hz
//   RST  (PB3, J2.14)  0.5 Hz
//
// A line that does not move is a break between the pin and the panel. Check the
// amplitude while you are there: the ATtiny runs at 5V and the panel at 3.3V,
// with nothing in between on this board, so a swing that tops out near 3.9V
// instead of 5V means the panel's protection diodes are clamping and the module
// needs a level shifter.
__attribute__((unused)) static void pinTestLoop() {
	SPI0.CTRLA = 0;						// hand PA1/PA3 back to the port

	uint8_t tick = 0;
	for(;;) {
		if(tick & 0x01) LCD_SPI_PORT.OUTSET = LCD_CS_bm;    else LCD_SPI_PORT.OUTCLR = LCD_CS_bm;
		if(tick & 0x02) LCD_SPI_PORT.OUTSET = LCD_MOSI_bm;  else LCD_SPI_PORT.OUTCLR = LCD_MOSI_bm;
		if(tick & 0x04) LCD_SPI_PORT.OUTSET = LCD_SCK_bm;   else LCD_SPI_PORT.OUTCLR = LCD_SCK_bm;
		if(tick & 0x08) LCD_CTRL_PORT.OUTSET = LCD_DC_bm;   else LCD_CTRL_PORT.OUTCLR = LCD_DC_bm;
		if(tick & 0x10) LCD_CTRL_PORT.OUTSET = LCD_RST_bm;  else LCD_CTRL_PORT.OUTCLR = LCD_RST_bm;

		tick++;
		_delay_ms(62);
	}
}

// Read the controller back and report it on a spare pin, because with a dead
// panel there is nowhere else to put the answer. This is the test that decides
// whether the controller is listening at all: everything up to now has only
// proved the ATtiny is talking.
//
// The read is half duplex on SDA, since the module has no SDO pin. Watch PB5
// (J2.18, spare) with the scope: each round is a 20ms high start marker, then
// 40 bits at 1ms each, MSB first, high = 1, so the five bytes can be read
// straight off the trace.
//
// Nothing is skipped for dummy clocks, so an answer may appear shifted a bit or
// a byte into the stream. What matters is the shape:
//
//   0x85 0x85 0x52 somewhere in it   an ST7789 answering. The controller is
//                                    alive and the fault is in the init values
//   any structured pattern           something is answering: send me the bytes
//   all ones                         nothing drives SDA back. Either the
//                                    controller ignores reads, or it is not
//                                    responding at all
//   all zeros                        SDA is being held low somewhere
__attribute__((unused)) static void idReportLoop() {
	LCD_CTRL_PORT.DIRSET = PIN5_bm;

	for(;;) {
		uint8_t id[5];
		lcdReadRegister(0x04, id, 5);	// RDDID

		LCD_CTRL_PORT.OUTSET = PIN5_bm;	// start marker
		_delay_ms(20);
		LCD_CTRL_PORT.OUTCLR = PIN5_bm;
		_delay_ms(5);

		for(uint8_t byte = 0; byte < 5; byte++) {
			for(uint8_t bit = 0; bit < 8; bit++) {
				if(id[byte] & (0x80 >> bit))
					LCD_CTRL_PORT.OUTSET = PIN5_bm;
				else
					LCD_CTRL_PORT.OUTCLR = PIN5_bm;
				_delay_ms(1);
			}
		}
		LCD_CTRL_PORT.OUTCLR = PIN5_bm;

		_delay_ms(500);
	}
}

// Panel bring-up mode. Set to 0 once the display works to get the text screen.
//
// This drops every assumption the text path makes. It re-runs lcdInit() on
// every pass, so RST pulses low once a second and the whole init sequence can
// be caught on a scope over and over instead of only in the first millisecond
// after power-up, and it floods the controller's entire frame memory rather
// than the 76x284 window, so neither the offsets nor the rotation can hide the
// result. Solid colours cycling on the panel means the controller is listening
// and only the addressing is wrong; a screen that stays white means it is not
// accepting commands at all.
//
//   0 = the real text screen
//   1 = flood the whole frame memory with cycling colours
//   2 = pin wiggle, no SPI at all, for checking the wiring end to end
//   3 = read the controller ID back and report it on PB5
//
// Set it in the Makefile (make DIAG_MODE=2); this is only the fallback for a
// compile that does not pass one.
#ifndef DIAG_MODE
	#define DIAG_MODE 1
#endif

__attribute__((unused)) static void diagLoop() {
	static const uint16_t colors[5] = { LCD_RED, LCD_GREEN, LCD_BLUE, LCD_BLACK, LCD_WHITE };

	uint8_t ix = 0;
	for(;;) {
		lcdInit();						// pulses RST every pass
		lcdFillRam(colors[ix]);
		ix++;
		if(ix >= 5)
			ix = 0;
		_delay_ms(1000);
	}
}

__attribute__((unused)) static void textLoop() {
	// Repaint forever rather than painting once and idling: on a scope or
	// analyser this gives a repeating SPI burst to trigger on, and on the panel
	// the counter proves the loop is still running even if the image is wrong.
	// The colour of the counter cycles too, so a frozen frame is obvious.
	static const uint16_t cycle[4] = { LCD_WHITE, LCD_GREEN, LCD_CYAN, LCD_MAGENTA };

	uint16_t frame = 0;
	for(;;) {
		drawTestScreen();

		char buf[6];
		formatNumber(buf, frame, 5);
		lcdDrawText_P(0, lineY(3), PSTR("FRAME"), LCD_GREY, LCD_BLACK);
		lcdDrawText(6 * FONT_CELL_W, lineY(3), buf, cycle[frame & 3], LCD_BLACK);

		frame++;
		_delay_ms(500);
	}
}

int main() {
	clockInit();
	lcdInit();

#if DIAG_MODE == 3
	idReportLoop();
#elif DIAG_MODE == 2
	pinTestLoop();
#elif DIAG_MODE == 1
	diagLoop();
#else
	textLoop();
#endif
}
