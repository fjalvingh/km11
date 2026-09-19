#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

#include "lcd.h"
#include "font5x7.h"
#include "pcf8574.h"
#include "signals.h"

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
static inline uint16_t lineY(uint8_t line, const Font *font = &FontSmall) {
	return (uint16_t) line * (font->height + 1);
}

static inline uint16_t lineX(uint8_t column, const Font *font = &FontSmall) {
	return (uint16_t) column * (font->width + 1);
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

static void formatHex(char *buf, uint16_t value, uint8_t width) {
	buf[width] = '\0';
	for(uint8_t i = width; i-- > 0;) {
		int c = (value % 16);
		if(c > 9)
			c += 'A' - 10;
		else
			c += '0';

		buf[i] = (char) c;

		value /= 16;
		if(value == 0 && i > 0) {
			while(i-- > 0)
				buf[i] = '0';
			break;
		}
	}
}

static void formatHex0x(char* buf, uint16_t value, uint16_t width) {
	*buf++  = '0';
	*buf++ = 'x';
	formatHex(buf, value, width);
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
// The modes are a ladder, each dropping more assumptions than the last: the
// text screen needs everything to be right, mode 1 needs the geometry but not
// the font, mode 4 needs neither, mode 2 needs no SPI at all.
//
//   0 = the real text screen
//   1 = fill the panel with cycling colours, through the normal path
//   2 = pin wiggle, no SPI at all, for checking the wiring end to end
//   3 = read the controller ID back and report it on PB5
//   4 = flood the raw frame memory, ignoring panel size and offsets
//
// Set it in the Makefile (make DIAG_MODE=2); this is only the fallback for a
// compile that does not pass one.
#ifndef DIAG_MODE
	#define DIAG_MODE 1
#endif

// No white and no black in the cycle, deliberately. A pixel the controller
// never writes keeps whatever it last received, so with white in the rotation
// every missed pixel ends up white and stays white - which reads as "white
// lines" and hides which frame actually lost them. Against these four, a stale
// pixel shows up as the wrong colour instead.
static const uint16_t diagColors[4] = { LCD_RED, LCD_GREEN, LCD_BLUE, LCD_YELLOW };

// Fills the panel through the normal drawing path: real geometry, real offsets,
// real rotation. Once the controller is known to be alive this is the honest
// test, because it exercises exactly what the text screen will use.
//
// Every other pass draws stripes instead of a flat fill, one lcdFillRect per
// stripe, which is what tells the two failure modes apart:
//
//   stale pixels land on stripe boundaries   whole transactions are being lost,
//                                            so the fault is in the command or
//                                            window handling
//   stale pixels scattered inside stripes    bytes are being dropped mid
//                                            stream: a timing or signal
//                                            integrity problem, so try
//                                            LCD_SPI_DIV 64 or 128
__attribute__((unused)) static void diagLoop() {
	uint8_t ix = 0;
	for(;;) {
		lcdFill(diagColors[ix & 3]);
		_delay_ms(1000);

		// Eight pixel stripes, alternating, each its own window and RAMWR.
		for(uint16_t y = 0; y < LCD_H; y += 8) {
			uint16_t h = (y + 8 > LCD_H) ? (LCD_H - y) : 8;
			lcdFillRect(0, y, LCD_W, h, ((y >> 3) & 1) ? LCD_MAGENTA : LCD_CYAN);
		}
		_delay_ms(1000);

		ix++;
	}
}

// Fills the whole frame memory instead, ignoring panel size and offsets. Keep
// this for a panel that shows nothing at all; it addresses more memory than the
// glass has, so gaps and edge artefacts here do not necessarily mean anything
// is wrong with the drawing path.
__attribute__((unused)) static void ramFloodLoop() {
	uint8_t ix = 0;
	for(;;) {
		lcdInit();						// pulses RST every pass
		lcdFillRam(diagColors[ix]);
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

static uint16_t flagsStartY = (FontLarge.height + 1) * 2 + 6;

static uint8_t pcfStatus;
static uint16_t currentBg = LCD_BLACK;
static uint16_t currentFlagX;
static uint16_t currentFlagY = flagsStartY;

static void flagTop(const Font *font = &FontSmall) {
	currentFlagY = flagsStartY;
	currentFlagX += lineX(6, font);
}

/**
 * Shows a flag either ON or OFF, and moves to the next FLAG position.
 */
// Takes the flag word masked, not a boolean, so the caller can pass
// (flags & SIG_x) straight in - hence uint16_t: the U7 signals live in the high
// byte and an uint8_t parameter would truncate every one of them to zero.
static void flagDisp(const char* name, uint16_t value, const Font *font = &FontSmall) {
	uint16_t color = value == 0 ? LCD_LIGHTRED : LCD_YELLOW;
	lcdDrawText_P(currentFlagX, currentFlagY, name, color, currentBg, font);
	currentFlagY += (font->height + 1);
	if(currentFlagY + font->height > LCD_H) {
		flagTop(font);
	}
}

// The last sample off the expanders. Everything the screen draws comes out of
// here, so a failed read leaves the previous frame's numbers on the glass
// rather than blanking them.
static KmSignals currentSignals;

static uint8_t getMPC() {
	return currentSignals.mpc;
}

static uint16_t getAMUX() {
	return currentSignals.amux;
}

static uint8_t getSPAD() {
	return currentSignals.spad;
}

static uint8_t getAluS() {
	return currentSignals.aluS;
}

static void space(uint16_t& x, const Font *font = &FontSmall) {
	x += (font->width + 1);
}

static void example() {
	uint16_t x = 0;
	uint16_t y = 0;
	char buf[20];
	// if(pcfStatus == 0) {
	// 	currentBg = LCD_BLACK;
	// } else {
	// 	currentBg = LCD_BLUE;
	// }

	//-- 1st 2 lines: large text
	const Font& font = FontLarge;
	x = lcdDrawText_P(x, y, PSTR("MPC"), LCD_WHITE, currentBg, &font);
	space(x, &font);
	formatNumber(buf, getMPC(), 2);
	x = lcdDrawText(x, y, buf, LCD_GREEN, currentBg, &font);

	space(x, &font);
	x = lcdDrawText_P(x, y, PSTR("AMUX "), LCD_WHITE, currentBg, &font);
	formatHex0x(buf, getAMUX(), 4);
	space(x, &font);
	x = lcdDrawText(x, y, buf, LCD_GREEN, currentBg, &font);

	//-- NEXT LINE
	y = lineY(1, &FontLarge);
	x = 0;

	x = lcdDrawText_P(x, y, PSTR("SPAD"), LCD_WHITE, currentBg, &font);
	formatHex(buf, getSPAD(), 1);
	space(x, &font);
	x = lcdDrawText(x, y, buf, LCD_GREEN, currentBg, &font);

	space(x, &font);
	x = lcdDrawText_P(x, y, PSTR("ALU_S"), LCD_WHITE, currentBg, &font);
	formatHex(buf, getAluS(), 1);
	space(x, &font);
	x = lcdDrawText(x, y, buf, LCD_GREEN, currentBg, &font);

	currentFlagX = lineX(0);
	currentFlagY = flagsStartY;			// flagDisp() leaves it wherever it ended

	uint16_t flags = currentSignals.flags;

	flagDisp(PSTR("ALUM"), flags & SIG_ALUM, &font);
	flagDisp(PSTR("CIN"), flags & SIG_CIN, &font);
	flagDisp(PSTR("EALU"), flags & SIG_EALU, &font);
	flagDisp(PSTR("SPWR"), flags & SIG_SPWR, &font);

	flagTop(&font);
	flagDisp(PSTR("AUXC"), flags & SIG_AUX_C, &font);
	flagDisp(PSTR("BUTJJ"), flags & SIG_BUT_JJ, &font);
	flagDisp(PSTR("BUTUN"), flags & SIG_BUT_UN, &font);
	flagDisp(PSTR("CNST"), flags & SIG_CNST, &font);

	flagTop(&font);
	flagDisp(PSTR("MSYN"), flags & SIG_MSYN, &font);
	flagDisp(PSTR("SSYN"), flags & SIG_SSYN, &font);
	flagDisp(PSTR("C1"), flags & SIG_C1, &font);
	flagDisp(PSTR("C2"), flags & SIG_C2, &font);

	//-- Last two
	// currentFlagY += font.height + 1;
	uint16_t last = currentFlagY;
	currentFlagX = 0;

	flagDisp(PSTR("BUTIR"), flags & SIG_BUT_IR, &font);

	currentFlagX = 80;
	currentFlagY = last;
	flagDisp(PSTR("BBSY"), flags & SIG_BBSY, &font);



}

// Reset diagnostics, shown in the bottom-right corner while RESET_DIAG is set
// (default on during bring-up; make EXTRA=-DRESET_DIAG=0 to hide it). RSTFR holds the cause of the last reset (PORF 01, BORF 02,
// EXTRF 04, WDRF 08, SWRF 10, UPDIRF 20) until cleared, and bootCount lives in
// .noinit so the C runtime does not zero it: it survives every reset except a
// power-on one. A screen that keeps re-initialising is the MCU restarting, and
// these two say how. A rising count with RSTFR empty is a crash that ran off
// the end of flash and back to address 0, not a hardware reset.
// Idle gap between frames. Overridable from the command line (make EXTRA=-DLOOP_DELAY_MS=1000)
// for experiments; the default is what the real screen runs with.
#ifndef LOOP_DELAY_MS
	#define LOOP_DELAY_MS 100
#endif
#ifndef RESET_DIAG
	#define RESET_DIAG 1
#endif

static uint8_t resetFlags;
static uint8_t bootCount __attribute__((section(".noinit")));

static void resetDiag() {
	resetFlags = RSTCTRL.RSTFR;
	RSTCTRL.RSTFR = resetFlags;			// write-1-to-clear, so each boot shows its own cause
	if(resetFlags & RSTCTRL_PORF_bm)
		bootCount = 0;
	bootCount++;
}

// Bottom-right corner, small font: "Rxx Bnn Fnnn". The frame counter keeps
// going only while the loop does: if the panel blanks and the count carries on
// across it, the fault is on the panel side, not a restarting MCU.
static uint16_t frameCount;

__attribute__((unused)) static void showResetDiag() {
	char buf[13];
	buf[0] = 'R';
	formatHex(buf + 1, resetFlags, 2);
	buf[3] = ' ';
	buf[4] = 'B';
	formatHex(buf + 5, bootCount, 2);
	buf[7] = ' ';
	buf[8] = 'F';
	formatHex(buf + 9, frameCount++, 3);
	lcdDrawText(LCD_W - 12 * FONT_CELL_W, LCD_H - FONT_HEIGHT, buf, LCD_CYAN, LCD_BLACK);
}

int main() {
	resetDiag();
	clockInit();
	lcdInit();

#if DIAG_MODE == 4
	ramFloodLoop();
#elif DIAG_MODE == 3
	idReportLoop();
#elif DIAG_MODE == 2
	pinTestLoop();
#elif DIAG_MODE == 1
	diagLoop();
#else
#ifndef NO_I2C
	pcfInit();
#endif
	lcdFill(LCD_BLACK);					// Clear screen
	for(;;) {
		// NO_I2C (make EXTRA=-DNO_I2C) leaves the expanders alone entirely, to
		// separate anything the bus does from anything the drawing does.
#ifndef NO_I2C
		pcfStatus = signalsRead(&currentSignals);
#else
		(void) pcfStatus;
#endif
		example();
#if RESET_DIAG
		showResetDiag();
#endif
		_delay_ms(LOOP_DELAY_MS);
	}
	// textLoop();
#endif
}
