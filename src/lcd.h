#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
#include <avr/pgmspace.h>

#include "lcd_config.h"
#include "font.h"

// Minimal ST7789 driver for the KM11 module.
//
// There is deliberately no frame buffer: a 76x284 16-bit frame would be 43k,
// which is twenty times the RAM the ATtiny1616 has. Everything is streamed
// straight out of SPI0 while it is being computed, so the only RAM this driver
// uses is a handful of locals. Text is drawn a glyph at a time: each character
// opens its own 6x8 window and pushes exactly 48 pixels.

// Visible size after LCD_ROTATION has been applied.
#if (LCD_ROTATION == 1) || (LCD_ROTATION == 3)
	#define LCD_W LCD_PANEL_H
	#define LCD_H LCD_PANEL_W
#else
	#define LCD_W LCD_PANEL_W
	#define LCD_H LCD_PANEL_H
#endif

// RGB565, which is what the panel is configured for (COLMOD 0x55).
static inline constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
	return (uint16_t)((r & 0xF8) << 8) | (uint16_t)((g & 0xFC) << 3) | (uint16_t)(b >> 3);
}

static constexpr uint16_t LCD_BLACK    = 0x0000;
static constexpr uint16_t LCD_WHITE    = 0xFFFF;
static constexpr uint16_t LCD_RED      = 0xF800;
static constexpr uint16_t LCD_LIGHTRED = 0xFC10;	// rgb565(255, 128, 128), red washed towards white
static constexpr uint16_t LCD_GREEN    = 0x07E0;
static constexpr uint16_t LCD_BLUE     = 0x001F;
static constexpr uint16_t LCD_YELLOW   = 0xFFE0;
static constexpr uint16_t LCD_CYAN     = 0x07FF;
static constexpr uint16_t LCD_MAGENTA  = 0xF81F;
static constexpr uint16_t LCD_GREY     = 0x8410;

// Brings up SPI0 and the panel. Call once, after the clock is set up.
void lcdInit();

// Solid areas. Coordinates are in visible-screen space and are not clipped,
// so keep them inside LCD_W / LCD_H.
void lcdFillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcdFill(uint16_t color);

// Bring-up aid: floods the controller's entire 240x320 frame memory, ignoring
// the panel size, the offsets and the rotation. Wherever the visible window
// sits inside that memory, this covers it, so it separates "the controller is
// not listening" from "the controller is listening but I am addressing the
// wrong pixels". Takes about a second at 1.25MHz.
void lcdFillRam(uint16_t color);

// Bring-up aid: sends a command and clocks `count` bytes of the answer back in
// over SDA, which is the only way to read from a module that has no SDO pin.
// This is the one test that proves traffic goes *both* ways. Bit-banged, so it
// takes SPI0 down and restores it; do not call it from drawing code.
//
// Nothing is skipped for dummy clocks - the raw stream comes back as it arrives,
// because how many a controller inserts before its answer varies. Useful
// commands: 0x04 RDDID, 0x09 RDDST, 0x0A RDDPM (power mode).
void lcdReadRegister(uint8_t cmd, uint8_t *out, uint8_t count);

// Draws one glyph in a font->cellW x font->height cell (times scale) with the
// background painted in the same pass, and returns the x of the next cell.
// Characters outside the font's range are drawn as a blank cell.
//
// `font` is FontSmall (5x7 in a 6x8 cell) or FontLarge (8x16); scale is on top
// of that, so FontSmall at scale 2 and FontLarge are the same height but the
// latter is a real font rather than doubled pixels.
uint16_t lcdDrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, const Font *font = &FontSmall, uint8_t scale = 1);

// Draws a NUL terminated string, returning the x just past the last cell.
// lcdDrawText_P takes a string in flash: use it for fixed text, it keeps the
// literal out of RAM.
uint16_t lcdDrawText(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, const Font *font = &FontSmall, uint8_t scale = 1);
uint16_t lcdDrawText_P(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, const Font *font = &FontSmall, uint8_t scale = 1);

#endif // LCD_DRIVER_H
