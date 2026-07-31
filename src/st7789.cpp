#include <avr/io.h>
#include <util/delay.h>

#include "st7789.h"
#include "font5x7.h"

// ---------------------------------------------------------------- registers

#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_NORON   0x13
#define ST_INVON   0x21
#define ST_DISPON  0x29
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

static inline void spiWrite(uint8_t v) {
	SPI0.DATA = v;
	while(!(SPI0.INTFLAGS & SPI_IF_bm))
		;
	(void) SPI0.DATA;					// clears IF
}

static inline void spiWrite16(uint16_t v) {
	spiWrite((uint8_t) (v >> 8));
	spiWrite((uint8_t) v);
}

static inline void csLow()    { LCD_SPI_PORT.OUTCLR = LCD_CS_bm; }
static inline void csHigh()   { LCD_SPI_PORT.OUTSET = LCD_CS_bm; }
static inline void dcCommand(){ LCD_CTRL_PORT.OUTCLR = LCD_DC_bm; }
static inline void dcData()   { LCD_CTRL_PORT.OUTSET = LCD_DC_bm; }

static void writeCommand(uint8_t cmd) {
	dcCommand();
	spiWrite(cmd);
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

void lcdInit() {
	// CS, DC and RST idle high; MOSI and SCK are driven by SPI0 but still need
	// to be outputs.
	LCD_SPI_PORT.OUTSET = LCD_CS_bm;
	LCD_SPI_PORT.DIRSET = LCD_CS_bm | LCD_MOSI_bm | LCD_SCK_bm;
	LCD_CTRL_PORT.OUTSET = LCD_DC_bm | LCD_RST_bm;
	LCD_CTRL_PORT.DIRSET = LCD_DC_bm | LCD_RST_bm;

	// Master, mode 0, F_CPU/2 (10MHz at 20MHz). SSD frees PA4 from its slave
	// select duty so it can be used as CS by hand.
	SPI0.CTRLB = SPI_SSD_bm | SPI_MODE_0_gc;
	SPI0.CTRLA = SPI_MASTER_bm | SPI_CLK2X_bm | SPI_PRESC_DIV4_gc | SPI_ENABLE_bm;

	// Hardware reset.
	LCD_CTRL_PORT.OUTCLR = LCD_RST_bm;
	_delay_ms(10);
	LCD_CTRL_PORT.OUTSET = LCD_RST_bm;
	_delay_ms(120);

	csLow();

	writeCommand(ST_SWRESET);
	_delay_ms(150);

	writeCommand(ST_SLPOUT);
	_delay_ms(120);

	writeCommand(ST_COLMOD);
	spiWrite(0x55);						// 16 bit/pixel, RGB565

	writeCommand(ST_MADCTL);
	spiWrite(MADCTL_VALUE);

	writeCommand(ST_INVON);				// these IPS panels are inverted
	_delay_ms(10);

	writeCommand(ST_NORON);
	_delay_ms(10);

	writeCommand(ST_DISPON);
	_delay_ms(120);

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
