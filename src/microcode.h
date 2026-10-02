#ifndef MICROCODE_H
#define MICROCODE_H

#include <stdint.h>

// One KD11-B microword, reduced to what the KM11 lamps can confirm. Bits are
// as printed in DEC's listing, so the active low fields (ALU S*, AUX*) are
// still active low here. Generated into microcode.cpp by gen_microcode.py.
struct MicroWord {
	uint8_t nxt;			// next microaddress (NXT, already complemented)
	uint8_t ctl;			// MW_BUT, MW_LISTED, MW_SAM_ROM, MW_CIN
	uint8_t alu;			// MW_ALU (S3* S2* S1* S0* M), MW_AUX_E, MW_AUX_F
	uint8_t spa;			// scratchpad address from the word, 0..15
};

#define MW_BUT		0x0f	// branch microtest, BUT_* below
#define MW_LISTED	0x10	// the location is in the listing
#define MW_SAM_ROM	0x20	// the scratchpad address comes from the word, not IR or BA
#define MW_CIN		0x40	// CIN, active high

#define MW_ALU		0x1f	// the ALU field: S3* is bit 4, S0* bit 1, M bit 0
#define MW_AUX_E	0x20	// AUX CONTROL L in rev E (1973): zero is asserted
#define MW_AUX_F	0x40	// the same in rev F (1976)

// BUT values (KD11-B manual table 2-1). The others branch: the conditions they
// test are wire-ORed onto the active low MPC lines, so a branch can only add
// one bits to NXT.
#define BUT_IR_CLK		000
#define BUT_ENOFLO		004
#define BUT_IR_DECODE	007
#define BUT_SSYNC		010
#define BUT_DEST		011
#define BUT_UNARY		012
#define BUT_JMP_JSR		013
#define BUT_CONST		015
#define BUT_INIT		016
#define BUT_NON			017

// Plain const: on this core avr-gcc keeps .rodata in flash and maps it into
// the data space, so no pgm_read is needed.
extern const MicroWord microcode[256];

#endif
