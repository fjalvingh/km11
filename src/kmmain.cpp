#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "lcd.h"
#include "font5x7.h"
#include "pcf8574.h"
#include "signals.h"
#include "microtags.h"
#include "mcheck.h"

// The 20MHz fuse only selects the oscillator; the main clock still comes out of
// a divide-by-6 prescaler after reset, so the prescaler has to be set here for
// F_CPU (and with it _delay_ms and the SPI clock) to mean anything.
//
// It is set to divide by two, NOT switched off. U1 runs on +5V, so the part
// would be in spec at 20MHz, but nothing here needs it: 10MHz is plenty, the
// SPI and I2C dividers are tuned to it, and it keeps the edges on the ribbon
// gentler. If fuse2 is left at the 16MHz default this gives 8MHz instead,
// which is also fine - only the timings scale.
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

// Zero padded octal, which is what everything PDP-11 is written in: the MPC
// addresses in the microprogram flow charts are octal, and so are the console
// lights the AMUX value gets compared with.
static void formatOctal(char *buf, uint16_t value, uint8_t width) {
	buf[width] = '\0';
	for(uint8_t i = width; i-- > 0;) {
		buf[i] = (char) ('0' + (value & 7));
		value >>= 3;
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
//   CS   (PA4, J2.19, kmdisp J1.7)  8 Hz
//   MOSI (PA1, J2.13, kmdisp J1.4)  4 Hz
//   SCK  (PA3, J2.11, kmdisp J1.3)  2 Hz
//   DC   (PB2, J2.17, kmdisp J1.6)  1 Hz
//   RST  (PB3, J2.15, kmdisp J1.5)  0.5 Hz
//
// A line that does not move is a break between the pin and the panel. Check the
// amplitude while you are there: the lines leave the ATtiny at 5V and go
// through U10 (74LVC245 on 3.3V), so a 3.3V swing at the display end is right
// and a 5V one means the signal is bypassing U10.
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
// (U1 pin 6, spare, not on the ribbon) with the scope: each round is a 20ms
// high start marker, then 40 bits at 1ms each, MSB first, high = 1, so the
// five bytes can be read straight off the trace.
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

__attribute__((unused)) static uint8_t pcfStatus;
static uint16_t currentBg = LCD_BLACK;

// Background behind a value that differs from the machine state before the
// last change, so a single step shows what it moved. Dark enough that the
// red, yellow, green and cyan text stays readable on it.
static constexpr uint16_t LCD_HILITE = rgb565(0, 0, 150);

// Shows a flag in place: asserted is upper case in yellow, negated lower case
// in red, so the state reads even without the colour. Works on a RAM copy
// because it changes the case. Returns the x just past the text.
static uint16_t flagText(uint16_t x, uint16_t y, char *name, bool on, uint8_t style, const Font *font = &FontSmall);

// Same, for a name in flash.
static uint16_t flagDisp(uint16_t x, uint16_t y, const char *name, bool on, uint8_t style, const Font *font = &FontSmall) {
	char buf[8];
	strncpy_P(buf, name, sizeof buf - 1);
	buf[sizeof buf - 1] = '\0';
	return flagText(x, y, buf, on, style, font);
}

// How a value is drawn: plain, changed by the last step (dark blue behind
// it), or contradicting the microcode listing (white on red, which wins).
enum Style : uint8_t { ST_PLAIN, ST_CHANGED, ST_BAD };

static uint8_t styleOf(bool changed, bool bad) {
	return bad ? ST_BAD : changed ? ST_CHANGED : ST_PLAIN;
}

static uint16_t styleBg(uint8_t style) {
	return style == ST_BAD ? LCD_RED : style == ST_CHANGED ? LCD_HILITE : currentBg;
}

static uint16_t styleFg(uint8_t style, uint16_t fg) {
	return style == ST_BAD ? LCD_WHITE : fg;
}

static uint16_t flagText(uint16_t x, uint16_t y, char *name, bool on, uint8_t style, const Font *font) {
	if(!on) {
		for(char *p = name; *p != '\0'; p++)
			if(*p >= 'A' && *p <= 'Z')
				*p += 'a' - 'A';
	}
	return lcdDrawText(x, y, name, styleFg(style, on ? LCD_YELLOW : LCD_LIGHTRED), styleBg(style), font);
}

// The last sample off the expanders. Everything the screen draws comes out of
// here, so a failed read leaves the previous frame's numbers on the glass
// rather than blanking them.
__attribute__((unused)) static KmSignals currentSignals;
// The state before the last change, which drawScreen() highlights against.
__attribute__((unused)) static KmSignals previousSignals;

// Pads buf with spaces to `width` characters, so a short text overwrites a
// longer one left by the previous frame.
static void padTo(char *buf, uint8_t width) {
	uint8_t n = (uint8_t) strlen(buf);
	while(n < width)
		buf[n++] = ' ';
	buf[n] = '\0';
}

// What the processor keeps in each scratchpad register (KD11-B manual, Table
// 4-3). R13..R16 are unused. Three cells each so a shorter name overwrites a
// longer one.
static const char spadNames[16][4] PROGMEM = {
	"   ", "   ", "   ", "   ", "   ", "   ", "SP ", "PC ",
	"SRC", "DST", "VEC", "   ", "   ", "   ", "   ", "LAD",
};

// The 74181 functions as the KD11-B uses them: its A and B legs are active low
// (KD11-B manual 4.3.5), so this is the ACTIVE-LOW-DATA half of the 74181 truth
// table, where M high selects logic and CIN high adds one. Indexed by S3..S0 as
// the screen shows them (already inverted from the ALU S* lamps). "+" is
// addition, "|" OR; every DEC name in the microcode listing checks out against
// this table (A plus B = 9, A minus B minus 1 = 6, BL = 10, ABAR = 0, ...).
static const char aluLogic[16][7] PROGMEM = {
	"~A",     "~(A&B)", "~A|B",   "-1",     "~(A|B)", "~B",     "~(A^B)", "A|~B",
	"~A&B",   "A^B",    "B",      "A|B",    "0",      "A&~B",   "A&B",    "A",
};
// Arithmetic without carry in. With carry the result is one higher: a trailing
// "-1" is dropped, otherwise "+1" is appended.
static const char aluArith[16][11] PROGMEM = {
	"A-1",        "A&B-1",  "A&~B-1",     "-1",
	"A+(A|~B)",   "A&B+(A|~B)", "A-B-1",  "A|~B",
	"A+(A|B)",    "A+B",    "A&~B+(A|B)", "A|B",
	"A+A",        "A&B+A",  "A&~B+A",     "A",
};

// Cells left for the name on the ALU line. Five of the 32 arithmetic results
// have longer names ("A&~B+(A|B)+1" is 12); they show as "?", and the raw S,
// M and C beside them still say exactly what the ALU is doing. None of the
// five occurs in the microcode listing (the instruction-decode ROMs, which
// can also drive the ALU, are not transcribed).
#define ALU_NAME_W	9

// Names the ALU function into buf, space padded to ALU_NAME_W so it overwrites
// whatever the previous frame left there.
static void aluName(char *buf, uint8_t s, bool logic, bool carry) {
	uint8_t n;
	if(logic) {
		strcpy_P(buf, aluLogic[s]);
		n = strlen(buf);
	} else {
		strcpy_P(buf, aluArith[s]);
		n = strlen(buf);
		if(carry) {
			if(n == 2) {					// "-1" plus one
				strcpy(buf, "0");
				n = 1;
			} else if(buf[n - 2] == '-' && buf[n - 1] == '1') {
				n -= 2;
			} else {
				buf[n++] = '+';
				buf[n++] = '1';
			}
		}
	}
	if(n > ALU_NAME_W) {
		buf[0] = '?';
		n = 1;
	}
	while(n < ALU_NAME_W)
		buf[n++] = ' ';
	buf[n] = '\0';
}

// Everything on the main screen sits at a fixed place and is drawn only when
// what it shows changes, so a quiet machine costs no SPI traffic and a running
// one only repaints what moved. Each field remembers the key it was last drawn
// with: its value plus whether it is highlighted.
enum Field : uint8_t {
	F_MPC, F_AMUX, F_SPAD, F_SPWR, F_ALUS, F_ALUM, F_CIN, F_ALUNAME,
	F_EALU, F_AUXC, F_CNST, F_BUTIR, F_BUTJJ, F_BUTUN,
	F_MSYN, F_SSYN, F_BBSY, F_CYCLE, F_NXT, F_STATUS,
	F_COUNT
};

static uint32_t drawnKey[F_COUNT];
static bool screenDrawn;				// false: draw every field, and the labels

static bool fieldDue(Field f, uint32_t key) {
	if(screenDrawn && drawnKey[f] == key)
		return false;
	drawnKey[f] = key;
	return true;
}

static uint32_t fieldKey(uint16_t value, uint8_t style) {
	return (uint32_t) value | ((uint32_t) style << 16);
}

// Pixel layout: eight lines of the large font fill the 128 pixel height
// exactly, so lines are 16 apart with no extra gap. The font never uses the
// bottom row of its cell, which keeps the lines apart.
static const uint8_t ROW_H = 16;		// FontLarge.height
static const uint8_t GRID_Y = 3 * ROW_H;
static_assert(GRID_Y + 5 * ROW_H <= LCD_H, "the NXT line must be on the screen");
static const uint8_t GRID_COL1 = 45;
static const uint8_t GRID_COL2 = 99;

// One single-bit signal in the grid, highlighted when it changed.
struct GridFlag {
	const char *name;					// in flash
	uint16_t mask;
	uint8_t x;
	uint8_t row;
};

static const char nEALU[] PROGMEM = "EALU";
static const char nAUXC[] PROGMEM = "AUXC";
static const char nCNST[] PROGMEM = "CNST";
static const char nBUTIR[] PROGMEM = "BUTIR";
static const char nBUTJJ[] PROGMEM = "BUTJJ";
static const char nBUTUN[] PROGMEM = "BUTUN";
static const char nMSYN[] PROGMEM = "MSYN";
static const char nSSYN[] PROGMEM = "SSYN";
static const char nBBSY[] PROGMEM = "BBSY";

// One column per group: ALU and data path control, the microprogram branch
// tests, and the Unibus. In Field order from F_EALU on.
static const GridFlag gridFlags[] = {
	{ nEALU,  SIG_EALU,   0,         0 },
	{ nAUXC,  SIG_AUX_C,  0,         1 },
	{ nCNST,  SIG_CNST,   0,         2 },
	{ nBUTIR, SIG_BUT_IR, GRID_COL1, 0 },
	{ nBUTJJ, SIG_BUT_JJ, GRID_COL1, 1 },
	{ nBUTUN, SIG_BUT_UN, GRID_COL1, 2 },
	{ nMSYN,  SIG_MSYN,   GRID_COL2, 0 },
	{ nSSYN,  SIG_SSYN,   GRID_COL2, 1 },
	{ nBBSY,  SIG_BBSY,   GRID_COL2, 2 },
};

static void drawLabels(const Font *font) {
	lcdDrawText_P(0,  0,         PSTR("MPC"),  LCD_WHITE, currentBg, font);
	lcdDrawText_P(66, 0,         PSTR("AMUX"), LCD_WHITE, currentBg, font);
	lcdDrawText_P(0,  ROW_H,     PSTR("SPAD"), LCD_WHITE, currentBg, font);
	lcdDrawText_P(0,  2 * ROW_H, PSTR("ALU"),  LCD_WHITE, currentBg, font);
	lcdDrawText_P(32, 2 * ROW_H, PSTR("S"),    LCD_WHITE, currentBg, font);
	lcdDrawText_P(0,  GRID_Y + 4 * ROW_H, PSTR("NXT"), LCD_WHITE, currentBg, font);
}

// Draws `cur`, highlighting every value that differs from `prev`, the state
// before the last change. The highlight therefore stays until the machine
// moves again, which is what single stepping needs.
__attribute__((unused)) static void drawScreen(const KmSignals &cur, const KmSignals &prev, uint8_t status, const CheckResult &chk) {
	const Font *font = &FontLarge;
	char buf[20];
	uint16_t flags = cur.flags;
	uint16_t flagsChanged = cur.flags ^ prev.flags;
	uint8_t st;

	if(!screenDrawn)
		drawLabels(font);

	// The MPC is 8 bits, three octal digits: 000..377. It is the address of
	// the NEXT microstep, not the current one (KD11-B manual, 5.9 e). Marked
	// bad when the last step went somewhere the listing cannot go.
	st = styleOf(cur.mpc != prev.mpc, chk.state == CHK_SEQ);
	if(fieldDue(F_MPC, fieldKey(cur.mpc, st))) {
		formatOctal(buf, cur.mpc, 3);
		lcdDrawText(33, 0, buf, styleFg(st, LCD_GREEN), styleBg(st), font);
	}
	st = styleOf(cur.amux != prev.amux, false);
	if(fieldDue(F_AMUX, fieldKey(cur.amux, st))) {
		formatOctal(buf, cur.amux, 6);
		lcdDrawText(107, 0, buf, styleFg(st, LCD_GREEN), styleBg(st), font);
	}

	//-- SPAD: the register as DEC numbers it (R0..R17, octal), what the
	// processor keeps in it, and the write strobe.
	st = styleOf(cur.spad != prev.spad, chk.badSpad);
	if(fieldDue(F_SPAD, fieldKey(cur.spad, st))) {
		buf[0] = 'R';
		formatOctal(buf + 1, cur.spad, cur.spad > 7 ? 2 : 1);
		padTo(buf, 3);						// "R7 " overwrites a previous "R17"
		lcdDrawText(41, ROW_H, buf, styleFg(st, LCD_GREEN), styleBg(st), font);
		lcdDrawText_P(74, ROW_H, spadNames[cur.spad], styleFg(st, LCD_CYAN), styleBg(st), font);
	}
	st = styleOf(flagsChanged & SIG_SPWR, false);
	if(fieldDue(F_SPWR, fieldKey(flags & SIG_SPWR, st)))
		flagDisp(107, ROW_H, PSTR("SPWR"), flags & SIG_SPWR, st, font);

	//-- ALU: the raw select code, mode and carry in, then what they compute.
	// Glyph-wide gaps here, not space(), to leave ALU_NAME_W cells for the name.
	st = styleOf(cur.aluS != prev.aluS, chk.badAluS);
	if(fieldDue(F_ALUS, fieldKey(cur.aluS, st))) {
		formatHex(buf, cur.aluS, 1);
		lcdDrawText(40, 2 * ROW_H, buf, styleFg(st, LCD_GREEN), styleBg(st), font);
	}
	st = styleOf(flagsChanged & SIG_ALUM, chk.badFlags & SIG_ALUM);
	if(fieldDue(F_ALUM, fieldKey(flags & SIG_ALUM, st)))
		flagDisp(56, 2 * ROW_H, PSTR("M"), flags & SIG_ALUM, st, font);
	st = styleOf(flagsChanged & SIG_CIN, chk.badFlags & SIG_CIN);
	if(fieldDue(F_CIN, fieldKey(flags & SIG_CIN, st)))
		flagDisp(72, 2 * ROW_H, PSTR("C"), flags & SIG_CIN, st, font);

	// The ALU output only reaches the AMUX, and only while EALU selects it:
	// grey otherwise.
	uint16_t aluInputs = SIG_ALUM | SIG_CIN | SIG_EALU;
	st = styleOf(cur.aluS != prev.aluS || (flagsChanged & aluInputs), false);
	if(fieldDue(F_ALUNAME, fieldKey((uint16_t) (cur.aluS << 12) | (flags & aluInputs), st))) {
		aluName(buf, cur.aluS, flags & SIG_ALUM, flags & SIG_CIN);
		lcdDrawText(88, 2 * ROW_H, buf, flags & SIG_EALU ? LCD_GREEN : LCD_GREY, styleBg(st), font);
	}

	//-- The single-bit signals
	for(uint8_t i = 0; i < sizeof gridFlags / sizeof gridFlags[0]; i++) {
		const GridFlag &g = gridFlags[i];
		st = styleOf(flagsChanged & g.mask, chk.badFlags & g.mask);
		if(fieldDue((Field) (F_EALU + i), fieldKey(flags & g.mask, st)))
			flagDisp(g.x, GRID_Y + g.row * ROW_H, g.name, flags & g.mask, st, font);
	}

	// C1 C0 name the Unibus cycle (KD11-B Table 5-4), shown with its code.
	// Styled as a flag on MSYN, which is when the code is valid. Padded to
	// seven cells so a shorter name overwrites a longer one.
	static const char cycleNames[4][8] PROGMEM = { "DATI=0 ", "DATIP=1", "DATO=2 ", "DATOB=3" };
	uint16_t cycleInputs = SIG_C1 | SIG_C0 | SIG_MSYN;
	st = styleOf(flagsChanged & cycleInputs, false);
	if(fieldDue(F_CYCLE, fieldKey(flags & cycleInputs, st))) {
		uint8_t cycle = (flags & SIG_C1 ? 2 : 0) | (flags & SIG_C0 ? 1 : 0);
		strcpy_P(buf, cycleNames[cycle]);
		flagText(GRID_COL2, GRID_Y + 3 * ROW_H, buf, flags & SIG_MSYN, st, font);
	}

	//-- Bottom line: DEC's tag for the microstep the MPC points at. That is
	// the step about to execute, not the one whose effects are on the lamps,
	// hence NXT, the field name for it.
	const uint16_t yBottom = GRID_Y + 4 * ROW_H;
	st = styleOf(cur.mpc != prev.mpc, false);
	if(fieldDue(F_NXT, fieldKey(cur.mpc, st))) {
		strcpy_P(buf, microTags[cur.mpc]);
		if(buf[0] == '\0')					// not in the listing
			strcpy(buf, "?");
		padTo(buf, MICROTAG_LEN);
		lcdDrawText(33, yBottom, buf, LCD_CYAN, styleBg(st), font);
	}

	// Status, bottom right. Expanders that did not answer come first: their
	// inputs read as all ones, which decodes to plausible values, so this
	// says which part of the screen is not real. Otherwise the microcode
	// check: the revision (E, F, ? until a step has shown it) and whether the
	// lamps match the listing for the word on them.
	uint16_t key = status ? status : 0x100 | (uint16_t) (chk.state << 8) | (uint8_t) chk.rev;
	if(fieldDue(F_STATUS, key)) {
		uint16_t color = LCD_RED;
		uint8_t n = 0;
		if(status != 0) {
			strcpy(buf, "I2C:");
			n = 4;
			if(status == (1 << PCF_COUNT) - 1) {
				strcpy(buf + n, "ALL");
				n += 3;
			} else {
				for(uint8_t i = 0; i < PCF_COUNT; i++)
					if(status & (1 << i))
						buf[n++] = (char) ('2' + i);	// bit 0 is U2
			}
			buf[n] = '\0';
		} else {
			static const char checkNames[4][5] PROGMEM = { "--", "OK", "BAD", "SEQ?" };
			static const uint16_t checkColors[4] = { LCD_GREY, LCD_GREEN, LCD_RED, LCD_RED };
			buf[0] = chk.rev;
			buf[1] = ' ';
			strcpy_P(buf + 2, checkNames[chk.state]);
			color = checkColors[chk.state];
		}
		padTo(buf, 9);
		lcdDrawText(82, yBottom, buf, color, currentBg, font);
	}

	screenDrawn = true;
}

// Reset diagnostics, shown in the bottom-right corner while RESET_DIAG is set
// (default off; make EXTRA=-DRESET_DIAG=1 to show it). RSTFR holds the cause of the last reset (PORF 01, BORF 02,
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
	#define RESET_DIAG 0
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
	checkInit();
#endif
	lcdFill(LCD_BLACK);					// Clear screen
	for(;;) {
		// NO_I2C (make EXTRA=-DNO_I2C) leaves the expanders alone entirely, to
		// separate anything the bus does from anything the drawing does.
#ifndef NO_I2C
		KmSignals sample;
		pcfStatus = signalsRead(&sample);
		// Keep the state before the last change, not the last sample: when
		// single stepping the machine sits still between steps, and the
		// highlight has to stay until the next one.
		// The first sample has nothing to compare with and highlights nothing.
		static bool sampled;
		if(!sampled) {
			previousSignals = currentSignals = sample;
			sampled = true;
		} else if(!signalsEqual(sample, currentSignals)) {
			previousSignals = currentSignals;
			currentSignals = sample;
		}
		// A sample with an expander missing is not the machine: drop what the
		// check knows rather than learn from it.
		CheckResult check = { CHK_NONE, '?', 0, false, false };
		if(pcfStatus == 0)
			checkSample(sample, &check);
		else
			checkLost();
#else
		CheckResult check = { CHK_NONE, '?', 0, false, false };
#endif
		drawScreen(currentSignals, previousSignals, pcfStatus, check);
#if RESET_DIAG
		showResetDiag();
#endif
		_delay_ms(LOOP_DELAY_MS);
	}
	// textLoop();
#endif
}
