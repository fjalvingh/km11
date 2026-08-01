#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdint.h>

// The machine's state as the expanders see it, decoded from the six raw bytes
// that pcfReadAll() returns. See mapping.txt in the root of the repo for the
// authoritative version of the layout below.
//
// Everything in here is already the right way up: a signal whose name ends in a
// star in mapping.txt is active low on the backplane, and the decoder flips it,
// so a one in this struct always means asserted regardless of how the wire
// carries it.
struct KmSignals {
	uint8_t		mpc;			// MPC* 0..7,   U2 P0..P7, inverted
	uint16_t	amux;			// AMUX 0..15,  U3 then U4
	uint8_t		spad;			// SPAD 0..3,   U5 P0..P3
	uint8_t		aluS;			// ALU_S 0..3,  U5 P4..P7
	uint16_t	flags;			// the single bit signals, SIG_* below
};

// Bits within KmSignals.flags. The low byte is U6, the high byte U7; U7 P6 and
// P7 are not connected to anything, so bits 14 and 15 are always zero.
#define SIG_MSYN	0x0001		// U6 P0, MSYN*
#define SIG_SSYN	0x0002		// U6 P1, SSYN*
#define SIG_BBSY	0x0004		// U6 P2, BBSY*
#define SIG_BUT_IR	0x0008		// U6 P3, BUT_IR*
#define SIG_SPWR	0x0010		// U6 P4
#define SIG_EALU	0x0020		// U6 P5
#define SIG_CIN		0x0040		// U6 P6
#define SIG_ALUM	0x0080		// U6 P7
#define SIG_CNST	0x0100		// U7 P0, CNST*
#define SIG_BUT_UN	0x0200		// U7 P1, BUT_UN*
#define SIG_BUT_JJ	0x0400		// U7 P2, BUT_JJ*
#define SIG_AUX_C	0x0800		// U7 P3, AUX_C*
#define SIG_C2		0x1000		// U7 P4, C2*
#define SIG_C1		0x2000		// U7 P5, C1*

// Turns six raw expander bytes into a KmSignals. Split out from the read so it
// can be exercised without the hardware.
void signalsDecode(const uint8_t raw[6], KmSignals *out);

// Reads the six expanders and decodes them in one go. Returns the failure mask
// of pcfReadAll(), zero when all six answered. An expander that did not answer
// reads back as all ones, so its active low signals decode to unasserted and
// its active high ones to asserted - check the mask rather than trusting the
// values when it is not zero.
uint8_t signalsRead(KmSignals *out);

#endif
