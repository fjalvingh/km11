#include <avr/eeprom.h>
#include "mcheck.h"
#include "microcode.h"

// The microcode revision lives in EEPROM byte 0: 'E', 'F', anything else for
// "not known yet". Set or cleared with make REV=E|F|AUTO eeprom; uploading
// firmware erases it, and it is learnt again.
static uint8_t *const EE_REV = (uint8_t *) 0;

// The revisions differ only in AUX (and CKO, which has no lamp) in 14 words.
// The first time a step lands on one of them the AUXC lamp says which
// revision this is, and it is remembered.
static char rev = '?';

static KmSignals lastSample;
static bool haveSample;
static uint8_t moving;					// samples in a row that differed from the one before
static bool haveSettled;
static uint8_t settledMpc;				// the MPC of the last settled sample
static int16_t word = -1;				// address of the word on the lamps, -1 if unknown
static uint8_t state = CHK_NONE;
static CheckResult result;				// what the last settled sample showed

void checkInit() {
	uint8_t b = eeprom_read_byte(EE_REV);
	rev = (b == 'E' || b == 'F') ? (char) b : '?';
}

static bool branches(uint8_t but) {
	switch(but) {
		case BUT_NON:
		case BUT_CONST:
		case BUT_INIT:
		case BUT_SSYNC:
		case BUT_ENOFLO:
		case BUT_IR_CLK:
			return false;
		default:
			return true;
	}
}

// Whether a word can be followed by `next`: its NXT exactly, or for a branch,
// NXT with extra one bits from the conditions wire-ORed onto the MPC lines.
static bool canFollow(const MicroWord &w, uint8_t next) {
	if(!branches(w.ctl & MW_BUT))
		return next == w.nxt;
	return (next & w.nxt) == w.nxt;
}

// A new settled state: decide which word is on the lamps now.
static void settled(const KmSignals &s, bool ran) {
	if(!haveSettled || ran) {
		word = -1;
		state = CHK_NONE;
	} else if(s.mpc != settledMpc) {
		const MicroWord &w = microcode[settledMpc];
		if(!(w.ctl & MW_LISTED)) {
			word = -1;
			state = CHK_NONE;
		} else if(canFollow(w, s.mpc)) {
			word = settledMpc;
		} else {
			word = -1;
			state = CHK_SEQ;
		}
	}
	// Same MPC: a bus control clock (half a step) or a change outside the
	// control store. The word on the lamps is still the same one.
	haveSettled = true;
	settledMpc = s.mpc;
}

static void compare(const KmSignals &s) {
	CheckResult *r = &result;
	const MicroWord &w = microcode[word];
	uint8_t but = w.ctl & MW_BUT;
	uint16_t lamps = s.flags;
	uint16_t bad = 0;

	// BUT is decoded straight onto four lamps
	struct { uint8_t but; uint16_t sig; } const butLamps[] = {
		{ BUT_IR_DECODE, SIG_BUT_IR },
		{ BUT_JMP_JSR,   SIG_BUT_JJ },
		{ BUT_UNARY,     SIG_BUT_UN },
		{ BUT_CONST,     SIG_CNST },
	};
	for(uint8_t i = 0; i < sizeof butLamps / sizeof butLamps[0]; i++) {
		bool expect = but == butLamps[i].but;
		if(expect != ((lamps & butLamps[i].sig) != 0))
			bad |= butLamps[i].sig;
	}

	// The scratchpad address, when the word supplies it rather than IR or BA
	r->badSpad = (w.ctl & MW_SAM_ROM) && s.spad != w.spa;

	// AUX, which is where the revisions differ
	bool auxE = !(w.alu & MW_AUX_E);
	bool auxF = !(w.alu & MW_AUX_F);
	bool auxLamp = lamps & SIG_AUX_C;
	bool learn = false;
	bool aux;
	if(auxE == auxF)
		aux = auxE;
	else if(rev == 'E')
		aux = auxE;
	else if(rev == 'F')
		aux = auxF;
	else {
		aux = auxLamp;
		learn = true;
	}
	if(aux != auxLamp)
		bad |= SIG_AUX_C;

	// The ALU control lines are wire-ORed with the auxiliary ALU control and
	// the instruction decode, which take over when AUX is asserted and on the
	// SUB special case at BUT DEST (KD11-B manual 4.3.5, 4.4.2). Only check
	// the word's own values when neither can be driving them.
	r->badAluS = false;
	if(!aux && but != BUT_DEST) {
		uint8_t s3s0 = (uint8_t) (~(w.alu >> 1) & 0x0f);
		r->badAluS = s.aluS != s3s0;
		if(((w.alu & 1) != 0) != ((lamps & SIG_ALUM) != 0))
			bad |= SIG_ALUM;
		if(((w.ctl & MW_CIN) != 0) != ((lamps & SIG_CIN) != 0))
			bad |= SIG_CIN;
	}

	r->badFlags = bad;
	bool ok = bad == 0 && !r->badSpad && !r->badAluS;
	state = ok ? CHK_OK : CHK_BAD;

	// Learn the revision only from a word that otherwise checks out, so a
	// fault elsewhere does not get remembered as a revision.
	if(learn && ok) {
		rev = auxLamp == auxE ? 'E' : 'F';
		eeprom_update_byte(EE_REV, (uint8_t) rev);
	}
}

void checkLost() {
	haveSample = false;
	haveSettled = false;
	moving = 0;
	word = -1;
	state = CHK_NONE;
}

void checkSample(const KmSignals &s, CheckResult *r) {
	// Only trust a sample that equals the one before: the six expanders are
	// read one after another, so a read that straddles a step mixes two
	// states. A step shows as one or two changed samples; more in a row means
	// the clock is running and nothing can be checked. Until a sample
	// settles, the last result stands.
	if(!haveSample || !signalsEqual(s, lastSample)) {
		lastSample = s;
		haveSample = true;
		if(moving < 255)
			moving++;
		if(moving > 2) {
			word = -1;
			state = CHK_NONE;
		}
	} else if(moving > 0) {
		settled(s, moving > 2);
		moving = 0;
		if(word >= 0)
			compare(s);
	}
	if(word < 0) {
		result.badFlags = 0;
		result.badSpad = false;
		result.badAluS = false;
	}
	result.state = state;
	result.rev = rev;
	*r = result;
}
