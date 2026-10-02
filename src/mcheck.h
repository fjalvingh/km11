#ifndef MCHECK_H
#define MCHECK_H

#include <stdint.h>
#include "signals.h"

// Checks the KM11 lamps against the microcode listing while the machine is
// single stepped. The MPC lamps show the NEXT microaddress, so the word whose
// fields are on the other lamps is the one the MPC showed before the last
// step; that is only known after watching a step happen.

#define CHK_NONE	0		// nothing to check: no step seen, or the machine is running
#define CHK_OK		1		// the lamps match the listing
#define CHK_BAD		2		// some lamps do not match; see badFlags/badSpad/badAluS
#define CHK_SEQ		3		// the MPC moved somewhere the last word cannot branch to

struct CheckResult {
	uint8_t state;			// CHK_*
	char rev;				// microcode revision: 'E', 'F', or '?' until seen
	uint16_t badFlags;		// SIG_* lamps that disagree with the listing
	bool badSpad;
	bool badAluS;
};

// Loads the remembered revision from EEPROM.
void checkInit();

// Feeds one sample. Call once per read of the expanders.
void checkSample(const KmSignals &s, CheckResult *r);

// Forgets everything seen so far, for a sample that could not be read.
void checkLost();

#endif
