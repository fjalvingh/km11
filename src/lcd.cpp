#include <avr/io.h>
#include <util/delay.h>

#include "lcd.h"
#include "font5x7.h"

// ---------------------------------------------------------------- registers

#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_NORON   0x13
#define ST_INVOFF  0x20
#define ST_INVON   0x21
#define ST_DISPON  0x29
#define ST_RDDID   0x04
#define ST_CASET   0x2A
#define ST_RASET   0x2B
#define ST_RAMWR   0x2C
#define ST_MADCTL  0x36
#define ST_COLMOD  0x3A

// MADCTL bits.
#define MAD_MY  0x80
#define MAD_MX  0x40
#define MAD_MV  0x20
#define MAD_BGR 0x08

// Each controller has a fixed frame memory and the panel is a window inside it,
// so every address needs the offsets from lcd_config.h added. Rotating swaps
// the axes, and mirroring counts the offset from the other end.
#if LCD_CONTROLLER == LCD_ST7789
	#define RAM_W 240
	#define RAM_H 320
	#define COLMOD_16BIT 0x55			// RGB and MCU interface both 16 bit
#else
	#define RAM_W 132
	#define RAM_H 162
	#define COLMOD_16BIT 0x05			// ST7735 encodes 16 bit/pixel as 5
#endif

#if LCD_BGR
	#define MAD_ORDER MAD_BGR
#else
	#define MAD_ORDER 0x00
#endif

// MADCTL's MV bit swaps the axes, and the address windows follow it: in
// landscape, CASET spans what was the memory's long side and RASET the short
// one. Anything addressing raw frame memory has to use these rather than
// RAM_W/RAM_H, or it writes a window that is too short on one axis and runs off
// the end of the other.
#if (LCD_ROTATION == 1) || (LCD_ROTATION == 3)
	#define RAM_X_SPAN RAM_H
	#define RAM_Y_SPAN RAM_W
#else
	#define RAM_X_SPAN RAM_W
	#define RAM_Y_SPAN RAM_H
#endif

#if LCD_ROTATION == 0
	#define MADCTL_VALUE (MAD_ORDER)
	#define OFF_X LCD_COL_OFFSET
	#define OFF_Y LCD_ROW_OFFSET
#elif LCD_ROTATION == 1
	#define MADCTL_VALUE (MAD_MV | MAD_MX | MAD_ORDER)
	#define OFF_X LCD_ROW_OFFSET
	#define OFF_Y LCD_COL_OFFSET
#elif LCD_ROTATION == 2
	#define MADCTL_VALUE (MAD_MX | MAD_MY | MAD_ORDER)
	#define OFF_X (RAM_W - LCD_PANEL_W - LCD_COL_OFFSET)
	#define OFF_Y (RAM_H - LCD_PANEL_H - LCD_ROW_OFFSET)
#elif LCD_ROTATION == 3
	#define MADCTL_VALUE (MAD_MV | MAD_MY | MAD_ORDER)
	#define OFF_X (RAM_H - LCD_PANEL_H - LCD_ROW_OFFSET)
	#define OFF_Y (RAM_W - LCD_PANEL_W - LCD_COL_OFFSET)
#else
	#error "LCD_ROTATION must be 0..3"
#endif

// --------------------------------------------------------------- spi + pins

#if LCD_OPEN_DRAIN
// Diagnostic drive mode (make EXTRA=-DLCD_OPEN_DRAIN=1). The ATtiny sits on 5V
// and the panel on 3.3V with nothing between them, so every driven high pushes
// current through the panel's input clamps into its supply rail. Here a high is
// never driven: the pin is released and its internal pull-up (about 35k) lets
// the line rise, which limits that current to some 100uA. The hardware SPI
// cannot do this, so the bytes are bit-banged and the whole thing is slow -
// around 50kHz, the pull-up against the ribbon's capacitance sets the pace. If
// the picture holds in this mode and not in the normal one, the fault is the
// level mismatch, and series resistors or a level shifter are the cure.
static inline void odLow(PORT_t &port, uint8_t bm)  { port.OUTCLR = bm; port.DIRSET = bm; }
static inline void odHigh(PORT_t &port, uint8_t bm) { port.DIRCLR = bm; }

static inline void spiWrite(uint8_t v) {
	for(uint8_t bit = 0; bit < 8; bit++) {
		if(v & 0x80) odHigh(LCD_SPI_PORT, LCD_MOSI_bm); else odLow(LCD_SPI_PORT, LCD_MOSI_bm);
		_delay_us(8);						// let MOSI settle through the pull-up
		odHigh(LCD_SPI_PORT, LCD_SCK_bm);	// panel samples on the rising edge
		_delay_us(8);
		odLow(LCD_SPI_PORT, LCD_SCK_bm);
		v <<= 1;
	}
}

static inline void csLow()    { odLow(LCD_SPI_PORT, LCD_CS_bm); }
static inline void csHigh()   { odHigh(LCD_SPI_PORT, LCD_CS_bm); }
static inline void dcCommand(){ odLow(LCD_CTRL_PORT, LCD_DC_bm); }
static inline void dcData()   { odHigh(LCD_CTRL_PORT, LCD_DC_bm); }
static inline void rstLow()   { odLow(LCD_CTRL_PORT, LCD_RST_bm); }
static inline void rstHigh()  { odHigh(LCD_CTRL_PORT, LCD_RST_bm); }

// Pull-ups on, everything released; SPI0 stays off so the port owns PA1/PA3.
static void pinSetup() {
	LCD_SPI_PORT.PIN1CTRL = PORT_PULLUPEN_bm;
	LCD_SPI_PORT.PIN3CTRL = PORT_PULLUPEN_bm;
	LCD_SPI_PORT.PIN4CTRL = PORT_PULLUPEN_bm;
	LCD_CTRL_PORT.PIN2CTRL = PORT_PULLUPEN_bm;
	LCD_CTRL_PORT.PIN3CTRL = PORT_PULLUPEN_bm;
	odHigh(LCD_SPI_PORT, LCD_CS_bm | LCD_MOSI_bm);
	odLow(LCD_SPI_PORT, LCD_SCK_bm);		// mode 0: clock idles low
	odHigh(LCD_CTRL_PORT, LCD_DC_bm | LCD_RST_bm);
}

#else

static inline uint8_t spiTransfer(uint8_t v) {
	SPI0.DATA = v;
	while(!(SPI0.INTFLAGS & SPI_IF_bm))
		;
	return SPI0.DATA;					// reading DATA also clears IF
}

static inline void spiWrite(uint8_t v) {
	(void) spiTransfer(v);
}

static inline void csLow()    { LCD_SPI_PORT.OUTCLR = LCD_CS_bm; }
static inline void csHigh()   { LCD_SPI_PORT.OUTSET = LCD_CS_bm; }
static inline void dcCommand(){ LCD_CTRL_PORT.OUTCLR = LCD_DC_bm; }
static inline void dcData()   { LCD_CTRL_PORT.OUTSET = LCD_DC_bm; }
static inline void rstLow()   { LCD_CTRL_PORT.OUTCLR = LCD_RST_bm; }
static inline void rstHigh()  { LCD_CTRL_PORT.OUTSET = LCD_RST_bm; }

// CS, DC and RST idle high; MOSI and SCK are driven by SPI0 but still need to
// be outputs.
static void pinSetup() {
	LCD_SPI_PORT.OUTSET = LCD_CS_bm;
	LCD_SPI_PORT.DIRSET = LCD_CS_bm | LCD_MOSI_bm | LCD_SCK_bm;
	LCD_CTRL_PORT.OUTSET = LCD_DC_bm | LCD_RST_bm;
	LCD_CTRL_PORT.DIRSET = LCD_DC_bm | LCD_RST_bm;
}

#endif

static inline void spiWrite16(uint16_t v) {
	spiWrite((uint8_t) (v >> 8));
	spiWrite((uint8_t) v);
}

// Master, mode 0, F_CPU/16 (625kHz at 10MHz): slow enough to survive the wiring
// out to the display. Add SPI_CLK2X_bm to double it, or go to SPI_PRESC_DIV4_gc
// (+ CLK2X) for 2.5 / 5MHz once the link is trusted. SSD frees PA4 from its
// slave select duty so it can be used as CS by hand.
//
// Split out of lcdInit() because lcdReadRegister() has to switch SPI0 off to
// borrow the pins, and needs to put it back afterwards.
#if LCD_SPI_DIV == 4
	#define LCD_SPI_PRESC SPI_PRESC_DIV4_gc
#elif LCD_SPI_DIV == 16
	#define LCD_SPI_PRESC SPI_PRESC_DIV16_gc
#elif LCD_SPI_DIV == 64
	#define LCD_SPI_PRESC SPI_PRESC_DIV64_gc
#elif LCD_SPI_DIV == 128
	#define LCD_SPI_PRESC SPI_PRESC_DIV128_gc
#else
	#error "LCD_SPI_DIV must be 4, 16, 64 or 128"
#endif

static void spiSetup() {
#if LCD_OPEN_DRAIN
	SPI0.CTRLA = 0;
	return;
#endif
#if LCD_SPI_MODE == 3
	SPI0.CTRLB = SPI_SSD_bm | SPI_MODE_3_gc;
#else
	SPI0.CTRLB = SPI_SSD_bm | SPI_MODE_0_gc;
#endif
	SPI0.CTRLA = SPI_MASTER_bm | LCD_SPI_PRESC | SPI_ENABLE_bm;
}

// Every caller holds CS low around this. With LCD_DC_UNDER_CS the DC edges are
// moved to moments when CS is high: the panel ignores its clock while
// deselected, so a glitch that a DC edge couples into SCK on the ribbon can no
// longer count as a bit. The controller keeps its command state across a CS
// deassertion, only the byte counter resets, so the parameters and pixel data
// that follow still belong to the command.
static void writeCommand(uint8_t cmd) {
#if LCD_DC_UNDER_CS
	csHigh();
	dcCommand();
	_delay_us(1);
	csLow();
	spiWrite(cmd);
	csHigh();
	dcData();
	_delay_us(1);
	csLow();
#else
	dcCommand();
#if LCD_STRETCH_DC
	_delay_us(200);
#endif
	spiWrite(cmd);
#if LCD_STRETCH_DC
	_delay_us(200);
#endif
	dcData();
#endif
}

// Opens a write window and leaves the panel expecting pixel data.
static void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
	uint16_t x0 = x + OFF_X;
	uint16_t x1 = x0 + w - 1;
	uint16_t y0 = y + OFF_Y;
	uint16_t y1 = y0 + h - 1;

	writeCommand(ST_CASET);
	spiWrite16(x0);
	spiWrite16(x1);

	writeCommand(ST_RASET);
	spiWrite16(y0);
	spiWrite16(y1);

	writeCommand(ST_RAMWR);
}

// --------------------------------------------------------------------- init

#if LCD_INVERT
	#define ST_INV_CMD ST_INVON
#else
	#define ST_INV_CMD ST_INVOFF
#endif

// Init sequences are flash tables: command, argument count, arguments. Bit 7 of
// the count means "wait 120ms afterwards", and 0xFF ends the table. A table
// costs far less flash than the same sequence written out as calls, mostly
// because of the long gamma writes.

#if LCD_CONTROLLER == LCD_ST7789

#define ST_PORCTRL   0xB2
#define ST_GCTRL     0xB7
#define ST_VCOMS     0xBB
#define ST_LCMCTRL   0xC0
#define ST_VDVVRHEN  0xC2
#define ST_VRHS      0xC3
#define ST_VDVS      0xC4
#define ST_FRCTRL2   0xC6
#define ST_PWCTRL1   0xD0
#define ST_PVGAMCTRL 0xE0
#define ST_NVGAMCTRL 0xE1

static const uint8_t initFull[] PROGMEM = {
	ST_SLPOUT,   0x80,
	ST_COLMOD,   1, COLMOD_16BIT,
	ST_MADCTL,   1, MADCTL_VALUE,

	ST_PORCTRL,  5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
	ST_GCTRL,    1, 0x35,
	ST_VCOMS,    1, LCD_VCOM,
	ST_LCMCTRL,  1, 0x2C,
	ST_VDVVRHEN, 1, 0x01,
	ST_VRHS,     1, 0x12,
	ST_VDVS,     1, 0x20,
	ST_FRCTRL2,  1, 0x0F,				// 60Hz
	ST_PWCTRL1,  2, 0xA4, 0xA1,

	ST_PVGAMCTRL, 14, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
					  0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
	ST_NVGAMCTRL, 14, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
					  0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,

	ST_INV_CMD,  0,
	ST_NORON,    0,
	ST_DISPON,   0x80,
	0xFF
};

// The short sequence: enough for an ST7789V, not enough for every panel.
static const uint8_t initMinimal[] PROGMEM = {
	ST_SLPOUT,  0x80,
	ST_COLMOD,  1, COLMOD_16BIT,
	ST_MADCTL,  1, MADCTL_VALUE,
	ST_INV_CMD, 0,
	ST_NORON,   0,
	ST_DISPON,  0x80,
	0xFF
};

#else	// LCD_ST7735

#define ST_FRMCTR1 0xB1
#define ST_FRMCTR2 0xB2
#define ST_FRMCTR3 0xB3
#define ST_INVCTR  0xB4
#define ST_PWCTR1  0xC0
#define ST_PWCTR2  0xC1
#define ST_PWCTR3  0xC2
#define ST_PWCTR4  0xC3
#define ST_PWCTR5  0xC4
#define ST_VMCTR1  0xC5
#define ST_GMCTRP1 0xE0
#define ST_GMCTRN1 0xE1

// The standard ST7735R sequence. Unlike the ST7789 this controller has no
// usable defaults to fall back on, so there is no minimal variant: frame rate,
// power and gamma all have to be set or the panel shows nothing worth seeing.
static const uint8_t initFull[] PROGMEM = {
	ST_SLPOUT,   0x80,

	ST_FRMCTR1,  3, 0x01, 0x2C, 0x2D,	// frame rate, normal mode
	ST_FRMCTR2,  3, 0x01, 0x2C, 0x2D,	// idle mode
	ST_FRMCTR3,  6, 0x01, 0x2C, 0x2D,	// partial mode
					0x01, 0x2C, 0x2D,
	ST_INVCTR,   1, 0x07,				// line inversion

	ST_PWCTR1,   3, 0xA2, 0x02, 0x84,
	ST_PWCTR2,   1, 0xC5,
	ST_PWCTR3,   2, 0x0A, 0x00,
	ST_PWCTR4,   2, 0x8A, 0x2A,
	ST_PWCTR5,   2, 0x8A, 0xEE,
	ST_VMCTR1,   1, 0x0E,

	ST_COLMOD,   1, COLMOD_16BIT,
	ST_MADCTL,   1, MADCTL_VALUE,

	ST_GMCTRP1, 16, 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
					0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
	ST_GMCTRN1, 16, 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
					0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,

	ST_INV_CMD,  0,
	ST_NORON,    0,
	ST_DISPON,   0x80,
	0xFF
};

#endif

static void runInitTable(const uint8_t *table) {
	for(;;) {
		uint8_t cmd = pgm_read_byte(table++);
		if(cmd == 0xFF)
			break;

		uint8_t argc = pgm_read_byte(table++);
		writeCommand(cmd);
		for(uint8_t i = 0; i < (argc & 0x7F); i++)
			spiWrite(pgm_read_byte(table++));

		if(argc & 0x80)
			_delay_ms(120);
	}
}

void lcdInit() {
	pinSetup();
	spiSetup();

	// Hardware reset.
	rstLow();
	_delay_ms(10);
	rstHigh();
	_delay_ms(120);

	csLow();

	writeCommand(ST_SWRESET);
	_delay_ms(150);

#if LCD_FULL_INIT || LCD_CONTROLLER == LCD_ST7735
	runInitTable(initFull);
#else
	runInitTable(initMinimal);
#endif

	csHigh();
}

// -------------------------------------------------------------------- fills

void lcdFillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
	if(w == 0 || h == 0)
		return;

	uint8_t hi = (uint8_t) (color >> 8);
	uint8_t lo = (uint8_t) color;

	csLow();
	setWindow(x, y, w, h);
	for(uint16_t row = 0; row < h; row++) {
		for(uint16_t col = 0; col < w; col++) {
			spiWrite(hi);
			spiWrite(lo);
		}
	}
	csHigh();
}

void lcdFill(uint16_t color) {
	lcdFillRect(0, 0, LCD_W, LCD_H, color);
}

void lcdReadRegister(uint8_t cmd, uint8_t *out, uint8_t count) {
	// Half duplex on SDA: the module has no SDO, so the controller answers on
	// the same wire the command went out on. SPI0 cannot turn MOSI around
	// mid-transfer, so this is bit-banged and SPI0 is switched off for the
	// duration. Roughly 250kHz, well inside any controller's read timing, which
	// is always far slower than its write timing.
	SPI0.CTRLA = 0;						// release PA1 and PA3

	LCD_SPI_PORT.OUTCLR = LCD_SCK_bm;	// mode 0: clock idles low
	LCD_SPI_PORT.DIRSET = LCD_MOSI_bm | LCD_SCK_bm;

	csLow();
	dcCommand();
	for(int8_t bit = 7; bit >= 0; bit--) {
		if(cmd & (1 << bit))
			LCD_SPI_PORT.OUTSET = LCD_MOSI_bm;
		else
			LCD_SPI_PORT.OUTCLR = LCD_MOSI_bm;
		_delay_us(2);
		LCD_SPI_PORT.OUTSET = LCD_SCK_bm;
		_delay_us(2);
		LCD_SPI_PORT.OUTCLR = LCD_SCK_bm;
	}
	dcData();

	// Hand SDA over to the panel. The pull-up means a controller that does not
	// drive it reads back as a clean 0xFF rather than as floating noise.
	LCD_SPI_PORT.DIRCLR = LCD_MOSI_bm;
	LCD_SPI_PORT.PIN1CTRL = PORT_PULLUPEN_bm;
	_delay_us(2);

	// No dummy clocks are skipped: how many a controller inserts before the
	// answer varies between one bit and a whole byte, so the raw stream is
	// handed back as it arrives and the pattern can be found in it by eye.
	for(uint8_t i = 0; i < count; i++) {
		uint8_t v = 0;
		for(uint8_t bit = 0; bit < 8; bit++) {
			LCD_SPI_PORT.OUTSET = LCD_SCK_bm;
			_delay_us(2);
			v <<= 1;
			if(LCD_SPI_PORT.IN & LCD_MOSI_bm)
				v |= 1;
			LCD_SPI_PORT.OUTCLR = LCD_SCK_bm;
			_delay_us(2);
		}
		out[i] = v;
	}

	csHigh();

	LCD_SPI_PORT.PIN1CTRL = 0;
	LCD_SPI_PORT.DIRSET = LCD_MOSI_bm;
	spiSetup();
}

void lcdFillRam(uint16_t color) {
	uint8_t hi = (uint8_t) (color >> 8);
	uint8_t lo = (uint8_t) color;

	csLow();
	// Deliberately not setWindow(): no offsets and no panel size, so whatever
	// part of the frame memory the panel is wired to is covered. The rotation
	// cannot be ignored though - MADCTL has already been set, and the address
	// windows are in rotated space.
	writeCommand(ST_CASET);
	spiWrite16(0);
	spiWrite16(RAM_X_SPAN - 1);
	writeCommand(ST_RASET);
	spiWrite16(0);
	spiWrite16(RAM_Y_SPAN - 1);
	writeCommand(ST_RAMWR);

	for(uint16_t row = 0; row < RAM_Y_SPAN; row++) {
		for(uint16_t col = 0; col < RAM_X_SPAN; col++) {
			spiWrite(hi);
			spiWrite(lo);
		}
	}
	csHigh();
}

// --------------------------------------------------------------------- text

uint16_t lcdDrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, const Font *font, uint8_t scale) {
	if(scale == 0)
		scale = 1;

	uint8_t width = font->width;
	uint8_t cells = font->cellW;			// includes the spacing columns
	uint8_t rows = font->height;
	uint8_t bpc = font->bytesPerCol;
	if(cells > FONT_MAX_CELL_W)
		cells = FONT_MAX_CELL_W;

	// The glyph is a handful of flash bytes, one column at a time, plus any
	// blank spacing columns. Pulling them into RAM first costs sixteen bytes of
	// stack and saves re-reading flash for every scaled row. Sixteen rows is the
	// most a uint16_t column holds, which is what the 8x16 font needs.
	uint16_t cols[FONT_MAX_CELL_W];
	uint8_t ix = (uint8_t) c;
	if(ix < font->first || ix > font->last) {
		for(uint8_t i = 0; i < cells; i++)
			cols[i] = 0;
	} else {
		const uint8_t *glyph = font->data + (uint16_t) (ix - font->first) * width * bpc;
		for(uint8_t i = 0; i < cells; i++) {
			if(i >= width) {
				cols[i] = 0;
				continue;
			}
			uint16_t v = pgm_read_byte(glyph);
			glyph++;
			if(bpc > 1) {
				v |= (uint16_t) pgm_read_byte(glyph) << 8;
				glyph++;
			}
			cols[i] = v;
		}
	}

	uint16_t cellW = (uint16_t) cells * scale;
	uint16_t cellH = (uint16_t) rows * scale;

	csLow();
	setWindow(x, y, cellW, cellH);
	// Row major, because that is the order the panel consumes pixels in: for
	// every row of the cell we walk the column words and pick out one bit.
	for(uint8_t row = 0; row < rows; row++) {
		uint16_t mask = (uint16_t) 1 << row;
		for(uint8_t sy = 0; sy < scale; sy++) {
			for(uint8_t col = 0; col < cells; col++) {
				uint16_t px = (cols[col] & mask) ? fg : bg;
				for(uint8_t sx = 0; sx < scale; sx++)
					spiWrite16(px);
			}
		}
	}
	csHigh();

	return x + cellW;
}

uint16_t lcdDrawText(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, const Font *font, uint8_t scale) {
	while(*s != '\0')
		x = lcdDrawChar(x, y, *s++, fg, bg, font, scale);
	return x;
}

uint16_t lcdDrawText_P(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, const Font *font, uint8_t scale) {
	for(;;) {
		char c = (char) pgm_read_byte(s++);
		if(c == '\0')
			break;
		x = lcdDrawChar(x, y, c, fg, bg, font, scale);
	}
	return x;
}
