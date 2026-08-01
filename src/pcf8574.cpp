#include <avr/io.h>
#include <util/delay.h>

#include "pcf8574.h"

// I2C host driver for the six PCF8574T expanders, on TWI0.
//
// This does not use Arduino's Wire: Wire pulls in the whole megaTinyCore
// runtime (it derives from Stream, so Print, malloc and the virtual dispatch
// table come with it) and this firmware is bare metal with 2K of SRAM and no
// core to link against. What is here is the same transaction that
// Wire.requestFrom() and Wire.beginTransmission()/write()/endTransmission()
// would produce on the wire, written straight against TWI0.
//
// PB0 is SCL and PB1 is SDA, which is TWI0's default pin position, so PORTMUX
// stays untouched. R1 and R2 pull both lines up to +5V on the board, so the
// internal pull-ups are left off.

#define I2C_FREQ	100000L

// MBAUD sets the SCL half period in peripheral clocks: the two-clock setup and
// hold overhead of the peripheral is the "5" here. Bus rise time is ignored,
// which makes the real clock slightly slower than asked for - harmless, and the
// safer direction with 4.7k pull-ups on a card this size.
#define TWI_MBAUD	((uint8_t) (((F_CPU / I2C_FREQ) / 2) - 5))

// Every wait below is bounded, because a wedged expander holding SDA low would
// otherwise hang the display loop forever. At 100kHz a byte takes 90us, so a
// millisecond is far more room than any single transfer needs.
#define TWI_TIMEOUT_US	1000

// Waits for any of the flags in `mask` to come up in MSTATUS. Returns false on
// timeout, with the bus left for the caller to abandon.
static bool twiWait(uint8_t mask) {
	for(uint16_t i = 0; i < TWI_TIMEOUT_US; i++) {
		if(TWI0.MSTATUS & mask)
			return true;
		_delay_us(1);
	}
	return false;
}

static void twiStop() {
	TWI0.MCTRLB = TWI_ACKACT_NACK_gc | TWI_MCMD_STOP_gc;
}

static void twiInit() {
	TWI0.MBAUD = TWI_MBAUD;
	TWI0.MCTRLA = TWI_ENABLE_bm;

	// The host state machine comes out of reset in the UNKNOWN state and will
	// not start a transaction from there; forcing it to IDLE is what a bus
	// recovery does, and is correct here because nothing has driven the bus yet.
	TWI0.MSTATUS = TWI_BUSSTATE_IDLE_gc;
}

// Addresses `addr` for reading or writing and waits for the address phase to
// finish. Returns false if the slave did not acknowledge, if arbitration was
// lost, or on timeout - in every case with a STOP already sent.
static bool twiStart(uint8_t addr, bool read) {
	TWI0.MADDR = (uint8_t) ((addr << 1) | (read ? 1 : 0));

	// A write acknowledge raises WIF, a read that the slave answers raises RIF
	// once the first byte has clocked in; a NACK on either raises WIF.
	if(! twiWait(TWI_WIF_bm | TWI_RIF_bm)) {
		twiStop();
		return false;
	}
	if(TWI0.MSTATUS & (TWI_RXACK_bm | TWI_ARBLOST_bm | TWI_BUSERR_bm)) {
		twiStop();
		return false;
	}
	return true;
}

// Single byte write transaction: START, address, one data byte, STOP.
static bool twiWriteByte(uint8_t addr, uint8_t value) {
	if(! twiStart(addr, false))
		return false;

	TWI0.MDATA = value;
	if(! twiWait(TWI_WIF_bm)) {
		twiStop();
		return false;
	}
	bool ok = (TWI0.MSTATUS & (TWI_RXACK_bm | TWI_ARBLOST_bm | TWI_BUSERR_bm)) == 0;
	twiStop();
	return ok;
}

// Single byte read transaction: START, address, one data byte, NACK, STOP. The
// NACK on the last byte is what tells the slave to let go of SDA, and without
// smart mode the acknowledge action only happens when the command is written,
// so the STOP below carries it.
static bool twiReadByte(uint8_t addr, uint8_t *value) {
	if(! twiStart(addr, true))
		return false;

	*value = TWI0.MDATA;
	twiStop();
	return true;
}

uint8_t pcfInit() {
	twiInit();

	uint8_t failed = 0;
	for(uint8_t i = 0; i < PCF_COUNT; i++) {
		if(! twiWriteByte(PCF_BASE_ADDR + i, 0xff))
			failed |= (uint8_t) (1 << i);
	}
	return failed;
}

uint8_t pcfReadAll(uint8_t values[PCF_COUNT]) {
	uint8_t failed = 0;
	for(uint8_t i = 0; i < PCF_COUNT; i++) {
		if(! twiReadByte(PCF_BASE_ADDR + i, &values[i])) {
			values[i] = 0xff;
			failed |= (uint8_t) (1 << i);
		}
	}
	return failed;
}
