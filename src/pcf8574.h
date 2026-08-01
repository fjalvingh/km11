#ifndef PCF8574_H
#define PCF8574_H

#include <stdint.h>

// The six PCF8574T port expanders that read the backplane, U2..U7 on the
// schematic. Their address straps (A2 A1 A0, tied to GND or +5V) give the
// consecutive addresses 0x20..0x25:
//
//   U2  000  0x20      U5  011  0x23
//   U3  001  0x21      U6  100  0x24
//   U4  010  0x22      U7  101  0x25
//
// Index 0 of every byte array below is U2 at 0x20, index 5 is U7 at 0x25.
#define PCF_COUNT		6
#define PCF_BASE_ADDR	0x20

// Brings up TWI0 as host on PB0/PB1 and puts every pin of every expander into
// input mode. The PCF8574 has no direction register: a pin is an input when a
// one has been written to it, because the output driver is only a weak pull-up
// that an external signal can override. So "make it an input" means writing
// 0xFF, and that is all this does - but it has to be done after every power-up,
// since the port latch resets to 0xFF only on the chip's own power-on and any
// later write of a zero would clamp that line low.
//
// Returns a bitmask of the expanders that did not acknowledge, bit 0 = U2, so
// zero means all six answered.
uint8_t pcfInit();

// Reads all six expanders into `values`, values[0] being U2. An expander that
// does not answer leaves 0xFF (all inputs high, the idle state) in its slot and
// sets its bit in the returned mask; zero means all six were read.
uint8_t pcfReadAll(uint8_t values[PCF_COUNT]);

#endif
