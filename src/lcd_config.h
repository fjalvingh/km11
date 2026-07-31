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

// Panel: 2.25", 76x284 pixels, ST7789 controller.
#define LCD_PANEL_W     76
#define LCD_PANEL_H     284

// The ST7789 has 240x320 of frame memory; a 76x284 panel is wired to a window
// inside it, so every address has to be shifted. These assume the panel sits
// centred: (240-76)/2 = 82 and (320-284)/2 = 18. If the first text lands off
// screen or wraps, these two numbers are what to adjust.
#define LCD_COL_OFFSET  82
#define LCD_ROW_OFFSET  18

// 0 and 2 are portrait (76 wide, 284 tall), 1 and 3 are landscape
// (284 wide, 76 tall). Landscape gives 47 columns x 9 lines of 5x7 text,
// which is what the KM11 signal display needs.
#define LCD_ROTATION    1

#endif // LCD_CONFIG_H
