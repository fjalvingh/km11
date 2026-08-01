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

// ---------------------------------------------------------------- the panel
//
// Two panels are described below; LCD_CONTROLLER picks one and everything else
// follows from it. The ST7735 is the 1.8" bring-up display, the ST7789P3 is the
// one the KM11 board is actually designed around.

#define LCD_ST7789      0
#define LCD_ST7735      1

#define LCD_CONTROLLER  LCD_ST7735

#if LCD_CONTROLLER == LCD_ST7789

// M35-2.25TFT-lanban, 2.25", 76x284 pixels, ST7789P3 controller.
#define LCD_PANEL_W     76
#define LCD_PANEL_H     284

// The ST7789 has 240x320 of frame memory and this panel is wired to a window
// inside it, so every address has to be shifted. These assume it sits centred:
// (240-76)/2 = 82 and (320-284)/2 = 18. Never confirmed on hardware - the panel
// has not produced an image yet.
#define LCD_COL_OFFSET  82
#define LCD_ROW_OFFSET  18

#define LCD_BGR         0
#define LCD_INVERT      1

// VCOM sets the LCD bias, and a value the glass does not like means no visible
// contrast at all - a controller that runs, accepts pixels and still shows
// plain white. Panel specific, and the first thing to sweep on a white screen:
// 0x19, 0x1A, 0x20, 0x28, 0x35.
#define LCD_VCOM        0x35

#else

// ST7735 board. Which panel it carries decides the size and the offsets, and
// the two common ones do not agree on either.
//
// 80x160 is the 0.96" size, and it sits in the middle of the frame memory.
// 128x160 is the classic 1.8" board, and it starts at the origin. A 1.8" board
// is far more often 128x160, so if this one shows a band down one side, or the
// image is shifted right by about a quarter of the screen, set this to 0.
#define LCD_ST7735_80x160  0

#if LCD_ST7735_80x160
	#define LCD_PANEL_W     80
	#define LCD_PANEL_H     160

	// The ST7735 has 132x162 of frame memory and this panel sits centred in it:
	// (132-80)/2 = 26, and one row down.
	#define LCD_COL_OFFSET  26
	#define LCD_ROW_OFFSET  1
#else
	#define LCD_PANEL_W     128
	#define LCD_PANEL_H     160

	// 128x160 fills the memory from the origin. Some boards ("green tab") want
	// 2 and 1 instead; a two pixel shift at one edge is the symptom.
	#define LCD_COL_OFFSET  0
	#define LCD_ROW_OFFSET  0
#endif

// This panel is wired red-green-blue. With BGR set it showed red and blue
// exchanged and green untouched - blue came out red, yellow came out cyan -
// which is the signature of that one bit. Other modules of the same type are
// genuinely BGR, so this is worth flipping if a replacement swaps its primaries.
//
// Inversion is off: with it on, this one showed every primary as its
// complement - red as cyan, green as magenta, blue as yellow. Plenty of 80x160
// modules do want it on, so it is worth flipping if a replacement panel comes
// out looking negative.
#define LCD_BGR         0
#define LCD_INVERT      0

#endif

// 0 and 2 are portrait, 1 and 3 are landscape. Landscape on the ST7789P3 panel
// gives 47 columns x 9 lines of 5x7 text, which is what the KM11 signal display
// needs; on the 80x160 ST7735 it gives 26 columns x 10 lines.
#define LCD_ROTATION    1

// Send the full init sequence (porch/frame rate, gate, VCOM, power and gamma)
// instead of the bare minimum of sleep-out, pixel format and display-on. With
// the power and VCOM registers left at their reset values a controller will
// happily run and accept pixels while driving the glass to plain white.
//
// Only consulted for the ST7789: the ST7735 needs its full sequence, and the
// minimal one is not offered for it.
#define LCD_FULL_INIT   1

// SPI mode. ST7789 clocks data in on the rising edge, which is mode 0 for an
// idle-low clock; a few modules only work in mode 3. Try 3 if the panel
// ignores everything even though data and clock look right on a scope.
#define LCD_SPI_MODE    0

// SPI clock divider off F_CPU: 4, 16, 64 or 128. At the 10MHz this runs at,
// that is 2.5MHz, 625kHz, 156kHz or 78kHz.
//
// Turn this DOWN, not up, when pixels come out missing or scrambled: if the
// artefacts change with the clock they are a timing or signal integrity
// problem, and if they do not change at all the fault is somewhere else
// entirely. A full screen at 128 takes several seconds, which is fine for a
// test and useless for anything else.
#define LCD_SPI_DIV     16

// Debug aid. DC is normally low for exactly one byte time (6.4us at 1.25MHz),
// which is far too short to spot on a scope that is not triggered on it. Set
// this to 1 to hold DC low for an extra 200us around every command byte, which
// makes the command phase plainly visible. Harmless to the protocol: the panel
// only samples DC on the clock edges, and the clock is idle during the delay.
#define LCD_STRETCH_DC  0

#endif // LCD_CONFIG_H
