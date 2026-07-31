#include <avr/io.h>
#include <util/delay.h>

#include "st7789.h"
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

// The ST7789 always has 240x320 of frame memory; the panel is a window in it,
// so every address needs the offsets from lcd_config.h added. Rotating swaps
// the axes, and mirroring counts the offset from the other end.
#define RAM_W 240
#define RAM_H 320

#if LCD_ROTATION == 0
	#define MADCTL_VALUE 0x00
	#define OFF_X LCD_COL_OFFSET
	#define OFF_Y LCD_ROW_OFFSET
#elif LCD_ROTATION == 1
	#define MADCTL_VALUE (MAD_MV | MAD_MX)
	#define OFF_X LCD_ROW_OFFSET
	#define OFF_Y LCD_COL_OFFSET
#elif LCD_ROTATION == 2
	#define MADCTL_VALUE (MAD_MX | MAD_MY)
	#define OFF_X (RAM_W - LCD_PANEL_W - LCD_COL_OFFSET)
	#define OFF_Y (RAM_H - LCD_PANEL_H - LCD_ROW_OFFSET)
#elif LCD_ROTATION == 3
	#define MADCTL_VALUE (MAD_MV | MAD_MY)
	#define OFF_X (RAM_H - LCD_PANEL_H - LCD_ROW_OFFSET)
	#define OFF_Y (RAM_W - LCD_PANEL_W - LCD_COL_OFFSET)
#else
	#error "LCD_ROTATION must be 0..3"
#endif

// --------------------------------------------------------------- spi + pins

static inline uint8_t spiTransfer(uint8_t v) {
	SPI0.DATA = v;
	while(!(SPI0.INTFLAGS & SPI_IF_bm))
		;
	return SPI0.DATA;					// reading DATA also clears IF
}

static inline void spiWrite(uint8_t v) {
	(void) spiTransfer(v);
}

static inline void spiWrite16(uint16_t v) {
	spiWrite((uint8_t) (v >> 8));
	spiWrite((uint8_t) v);
}

static inline void csLow()    { LCD_SPI_PORT.OUTCLR = LCD_CS_bm; }
static inline void csHigh()   { LCD_SPI_PORT.OUTSET = LCD_CS_bm; }
static inline void dcCommand(){ LCD_CTRL_PORT.OUTCLR = LCD_DC_bm; }
static inline void dcData()   { LCD_CTRL_PORT.OUTSET = LCD_DC_bm; }

// Master, mode 0, F_CPU/16 (625kHz at 10MHz): slow enough to survive the wiring
// out to the display. Add SPI_CLK2X_bm to double it, or go to SPI_PRESC_DIV4_gc
// (+ CLK2X) for 2.5 / 5MHz once the link is trusted. SSD frees PA4 from its
// slave select duty so it can be used as CS by hand.
//
// Split out of lcdInit() because lcdReadRegister() has to switch SPI0 off to
// borrow the pins, and needs to put it back afterwards.
static void spiSetup() {
#if LCD_SPI_MODE == 3
	SPI0.CTRLB = SPI_SSD_bm | SPI_MODE_3_gc;
#else
	SPI0.CTRLB = SPI_SSD_bm | SPI_MODE_0_gc;
#endif
	SPI0.CTRLA = SPI_MASTER_bm | SPI_PRESC_DIV16_gc | SPI_ENABLE_bm;
}

static void writeCommand(uint8_t cmd) {
	dcCommand();
#if LCD_STRETCH_DC
	_delay_us(200);
#endif
	spiWrite(cmd);
#if LCD_STRETCH_DC
	_delay_us(200);
#endif
	dcData();
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

#define ST_PORCTRL  0xB2
#define ST_GCTRL    0xB7
#define ST_VCOMS    0xBB
#define ST_LCMCTRL  0xC0
#define ST_VDVVRHEN 0xC2
#define ST_VRHS     0xC3
#define ST_VDVS     0xC4
#define ST_FRCTRL2  0xC6
#define ST_PWCTRL1  0xD0
#define ST_PVGAMCTRL 0xE0
#define ST_NVGAMCTRL 0xE1

#if LCD_INVERT
	#define ST_INV_CMD ST_INVON
#else
	#define ST_INV_CMD ST_INVOFF
#endif

// Init sequence as a flash table: command, argument count, arguments. Bit 7 of
// the count means "wait 120ms afterwards", and 0xFF ends the table. A table
// costs far less flash than the same sequence written out as calls, mostly
// because of the two fourteen-byte gamma writes.
static const uint8_t initFull[] PROGMEM = {
	ST_SLPOUT,   0x80,
	ST_COLMOD,   1, 0x55,				// 16 bit/pixel, RGB565
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
	ST_COLMOD,  1, 0x55,
	ST_MADCTL,  1, MADCTL_VALUE,
	ST_INV_CMD, 0,
	ST_NORON,   0,
	ST_DISPON,  0x80,
	0xFF
};

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
	// CS, DC and RST idle high; MOSI and SCK are driven by SPI0 but still need
	// to be outputs.
	LCD_SPI_PORT.OUTSET = LCD_CS_bm;
	LCD_SPI_PORT.DIRSET = LCD_CS_bm | LCD_MOSI_bm | LCD_SCK_bm;
	LCD_CTRL_PORT.OUTSET = LCD_DC_bm | LCD_RST_bm;
	LCD_CTRL_PORT.DIRSET = LCD_DC_bm | LCD_RST_bm;

	spiSetup();

	// Hardware reset.
	LCD_CTRL_PORT.OUTCLR = LCD_RST_bm;
	_delay_ms(10);
	LCD_CTRL_PORT.OUTSET = LCD_RST_bm;
	_delay_ms(120);

	csLow();

	writeCommand(ST_SWRESET);
	_delay_ms(150);

#if LCD_FULL_INIT
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
	// Deliberately not setWindow(): no offsets, no rotation, no panel size.
	// Whatever part of the 240x320 memory the panel is wired to, it is in here.
	writeCommand(ST_CASET);
	spiWrite16(0);
	spiWrite16(RAM_W - 1);
	writeCommand(ST_RASET);
	spiWrite16(0);
	spiWrite16(RAM_H - 1);
	writeCommand(ST_RAMWR);

	for(uint16_t row = 0; row < RAM_H; row++) {
		for(uint16_t col = 0; col < RAM_W; col++) {
			spiWrite(hi);
			spiWrite(lo);
		}
	}
	csHigh();
}

// --------------------------------------------------------------------- text

uint16_t lcdDrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale) {
	if(scale == 0)
		scale = 1;

	// The glyph is five flash bytes, one per pixel column, plus one blank
	// column of spacing. Pulling them into RAM first costs six bytes of stack
	// and saves re-reading flash for every scaled row.
	uint8_t cols[FONT_CELL_W];
	uint8_t ix = (uint8_t) c;
	if(ix < FONT_FIRST_CHAR || ix > FONT_LAST_CHAR) {
		for(uint8_t i = 0; i < FONT_CELL_W; i++)
			cols[i] = 0;
	} else {
		const uint8_t *glyph = &Font5x7[(uint16_t) (ix - FONT_FIRST_CHAR) * FONT_WIDTH];
		for(uint8_t i = 0; i < FONT_WIDTH; i++)
			cols[i] = pgm_read_byte(glyph + i);
		cols[FONT_WIDTH] = 0;
	}

	uint16_t cellW = (uint16_t) FONT_CELL_W * scale;
	uint16_t cellH = (uint16_t) FONT_HEIGHT * scale;

	csLow();
	setWindow(x, y, cellW, cellH);
	// Row major, because that is the order the panel consumes pixels in: for
	// every row of the cell we walk the column bytes and pick out one bit.
	for(uint8_t row = 0; row < FONT_HEIGHT; row++) {
		for(uint8_t sy = 0; sy < scale; sy++) {
			for(uint8_t col = 0; col < FONT_CELL_W; col++) {
				uint16_t px = (cols[col] & (1 << row)) ? fg : bg;
				for(uint8_t sx = 0; sx < scale; sx++)
					spiWrite16(px);
			}
		}
	}
	csHigh();

	return x + cellW;
}

uint16_t lcdDrawText(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale) {
	while(*s != '\0')
		x = lcdDrawChar(x, y, *s++, fg, bg, scale);
	return x;
}

uint16_t lcdDrawText_P(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale) {
	for(;;) {
		char c = (char) pgm_read_byte(s++);
		if(c == '\0')
			break;
		x = lcdDrawChar(x, y, c, fg, bg, scale);
	}
	return x;
}
