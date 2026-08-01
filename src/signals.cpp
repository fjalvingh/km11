#include "signals.h"
#include "pcf8574.h"

// Which bits of each expander byte arrive inverted, from mapping.txt. XOR-ing
// with these is the whole of the active low handling.
//
//   U2  MPC* is inverted end to end
//   U3  AMUX 0..7, straight
//   U4  AMUX 8..15, straight
//   U5  SPAD and ALU_S, straight
//   U6  P0..P3 (MSYN* SSYN* BBSY* BUT_IR*) inverted, P4..P7 straight
//   U7  P0..P5 all inverted, P6 and P7 unconnected and masked off elsewhere
static const uint8_t invertMask[PCF_COUNT] = { 0xff, 0x00, 0x00, 0x00, 0x0f, 0x3f };

void signalsDecode(const uint8_t raw[PCF_COUNT], KmSignals *out) {
	uint8_t v[PCF_COUNT];
	for(uint8_t i = 0; i < PCF_COUNT; i++)
		v[i] = raw[i] ^ invertMask[i];

	out->mpc = v[0];
	out->amux = (uint16_t) v[1] | ((uint16_t) v[2] << 8);
	out->spad = v[3] & 0x0f;
	out->aluS = (uint8_t) (v[3] >> 4);

	// U7 P6 and P7 go nowhere, so mask them off rather than letting whatever
	// the floating pull-ups read show up as two phantom flags.
	out->flags = (uint16_t) v[4] | ((uint16_t) (v[5] & 0x3f) << 8);
}

uint8_t signalsRead(KmSignals *out) {
	uint8_t raw[PCF_COUNT];
	uint8_t failed = pcfReadAll(raw);
	signalsDecode(raw, out);
	return failed;
}
