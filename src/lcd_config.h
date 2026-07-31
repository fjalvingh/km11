#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

// Hardware configuration for the KM11 display, taken from km1105.kicad_sch.
//
// The ATtiny1616 talks to the panel over SPI0 on its default port A mux, so no
// PORTMUX change is needed:
//
//   PA1  MOSI  -> J2.4
//   PA2  MISO  -> J2.6   (unused by the display, it is write-only)
//   PA3  SCK   -> J2.8
//   PA4  SS    -> J2.10  used as CS under software control (SPI0 SSD is set)
//   PB2  DC    -> J2.12  command / data select
//   PB3  RST   -> J2.14  panel reset, active low
//
// The backlight is tied to a fixed voltage on the display module, so there is
// no pin for it here. PB4 (J2.16) and PB5 (J2.18) are spare.

#define LCD_SPI_PORT    PORTA
#define LCD_MOSI_bm     PIN1_bm
#define LCD_SCK_bm      PIN3_bm
#define LCD_CS_bm       PIN4_bm

#define LCD_CTRL_PORT   PORTB
#define LCD_DC_bm       PIN2_bm
#define LCD_RST_bm      PIN3_bm

// Panel: M35-2.25TFT-lanban, 2.25", 76x284 pixels, ST7789P3 controller.
#define LCD_PANEL_W     76
#define LCD_PANEL_H     284

// The ST7789 has 240x320 of frame memory; a 76x284 panel is wired to a window
// inside it, so every address has to be shifted. These assume the panel sits
// centred: (240-76)/2 = 82 and (320-284)/2 = 18. If the first text lands off
// screen or wraps, these two numbers are what to adjust.
#define LCD_COL_OFFSET  0 //82
#define LCD_ROW_OFFSET  0 //18

// 0 and 2 are portrait (76 wide, 284 tall), 1 and 3 are landscape
// (284 wide, 76 tall). Landscape gives 47 columns x 9 lines of 5x7 text,
// which is what the KM11 signal display needs.
#define LCD_ROTATION    1

// Most IPS panels want display inversion on; some do not. If the colours come
// out as their opposites (a white background instead of black) set this to 0.
#define LCD_INVERT      1

// Send the full init sequence (porch, gate, VCOM, power and gamma) instead of
// the bare minimum of sleep-out, pixel format and display-on. The ST7789P3 in
// this panel is not reliable on the short sequence: with the power and VCOM
// registers left at their reset values the controller runs, accepts pixels and
// still drives the glass to plain white.
#define LCD_FULL_INIT   1

// VCOM setting used by the full init, and the first thing to try when the panel
// stays white or washed out despite everything else being right - it sets the
// LCD bias, and a value the glass does not like means no visible contrast at
// all. Panel specific; sweep it if in doubt. Common values across ST7789 panels
// are 0x19, 0x1A, 0x20, 0x28 and 0x35.
#define LCD_VCOM        0x35

// SPI mode. ST7789 clocks data in on the rising edge, which is mode 0 for an
// idle-low clock; a few modules only work in mode 3. Try 3 if the panel
// ignores everything even though data and clock look right on a scope.
#define LCD_SPI_MODE    0

// Debug aid. DC is normally low for exactly one byte time (6.4us at 1.25MHz),
// which is far too short to spot on a scope that is not triggered on it. Set
// this to 1 to hold DC low for an extra 200us around every command byte, which
// makes the command phase plainly visible. Harmless to the protocol: the panel
// only samples DC on the clock edges, and the clock is idle during the delay.
#define LCD_STRETCH_DC  0

#endif // LCD_CONFIG_H
