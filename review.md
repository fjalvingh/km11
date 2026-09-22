# Design review, 22 September 2026

A check of the card (`kicad/km1105`), the display board (`kicad/kmdisp`) and the firmware (`src/`)
against DEC's own PDP-11/05 documentation. The fixes listed at the end were applied the same day;
the findings are kept here as the record of what was checked and why.

## Sources

All scans on bitsavers are images without a text layer, and the server answers a plain `curl` with
403: fetch with a browser `User-Agent`.

- **EK-KD11B-MM-001**, *KD11-B Processor Maintenance Manual*, January 1975.
  `https://www.bitsavers.org/pdf/dec/pdp11/1105/EK-KD11B-MM-001_Jan75.pdf`.
  Section 5.9 *KM11 Maintenance Panel*, 5.10 *Using KM11 Maintenance Panel*, Figure 5-1 (the KM-1
  and KM-2 overlays with DEC's B/D marks: *bright* / *dim when asserted*) and Table 5-4 (what every
  lamp and switch means). PDF pages 97–100.
- **PDP-11/05 engineering drawings, rev AH**, July 1976.
  `https://www.bitsavers.org/pdf/dec/pdp11/1105/1105_RevAH_Engineering_Drawings_Jul76.pdf`.
  The overlay drawings themselves, A-SS-5509081-0-9 (KM1) and -0-10 (KM2), PDF pages 186–187; the
  microprogram flow K-MP-KD11-B-1, pages 34–45, which is where the MPC values come from.
- **KM11 Maintenance Panel**, May 1970.
  `https://www.bitsavers.org/pdf/dec/pdp11/KM11_Maintenance_Panel_May70.pdf`.
  The W130 / W131 prints, D-BS-KM11-0-MB: which connector pin drives which lamp, the lamp
  positions on the W131, and the switch circuit.
- Jörg Hoppe's KM11 replica, `/d1/hobby/MiniMainframes/pdp/joerg_km11/`: the schematic the `J1`
  pin names were taken from, and his 11/05 overlay.
- `kicad-cli sch erc` / `pcb drc` on both boards, a parsed netlist of both, and a full firmware
  rebuild (`make -B`).

## Verified correct

- **Every `J1` pin name matches the W130 print**, pin for pin: BSR0 H = R2, BSR1 H = E2,
  BSR3 H = H1, BSR7 H = L2, BSR15 H = N2, BSR14 H = D2, BSR12 H = H2, BSR8 H = J2, ISR0 H = S1,
  ISR1 H = M2, ISR3 H = F2, ISR7 H = E1, ISR15 H = R1, ISR14 H = K1, ISR12 H = S2, ISR8 H = M1,
  N H = P1, Z H = L1, V H = F1, C H = D1, T H = J1, TRAPS H = C1, B MSYN H = T2, B SSYN H = U2,
  R/W2 H = K2, TST1 H = N1, TST2 H = V1, ISR2 H = P2; M CLK L = U1, M CLK ENABLE L = V2,
  TIME OUT H = B2, BUS SSYN L = A1, +5 V = A2, +8 V = B1, GND = C2 and T1.
- **The whole of `km11-signals.txt`** — all 28 lamps for both KM-1 and KM-2 — matches DEC's
  overlays. The W131 placement drawing puts lamp I1 at the *top* of the right-hand column and I25
  at the top left, with switches S3 (BUS SSYN), S1 (B2), S4 (M CLK), S2 (M CLK ENABLE) in exactly
  the positions Figure 5-1 draws BUS SSYN / AC LO / M CLK PULSE / M CLK EN, which fixes the
  orientation. Every one of the 48 expander inputs in the netlist lands on the pin `mapping.txt`
  says, so the card reads precisely what the overlay shows. Nothing is swapped, nothing is missing.
- KM-1 is wired to row A and KM-2 to row B of the same slot (manual §5.9 g), so the double-height
  card's A connector = KM-1 = the unsuffixed nets, B = KM-2 = the `_2` nets.
- The KM-2 MSYN and SSYN lamps that are left unconnected are "same as MSYN on KM-1" (Table 5-4).
  The MPC "is duplicated on both KM11 slots", so reading it once is enough.
- Polarity: MPC (D), BUT IR / BBUSY / SSYN / MSYN (D), AUX C / BUT JJ / BUT UN / CNST (D),
  C1 / C0 (D) are inverted by the firmware; AMUX (B), SPAD (B), ALUM / CIN / EALU / SPWR (B) are
  not. All agree with the overlay — except ALU S, see below.
- PCF8574 addresses 0x20–0x25 match the A0–A2 strapping of U2–U7. TWI0 on PB0/PB1 and SPI0 on
  PA1/PA3/PA4 are the ATtiny1616 default positions. U10 (74LVC245): DIR high, OE low, unused A
  inputs grounded, 5 V-tolerant inputs on a 3.3 V supply.
- The switch circuit is the W131 circuit: a cross-coupled 7400 latch driving M CLK L straight from
  a totem-pole output, a transistor pulling BUS SSYN L, a pull-up on M CLK ENABLE L, none on B2.
  Driving `M_CLK_L` from a 74ACT00 output is therefore by the book. The manual (§4.5) confirms
  M CLK L is an input to E039 pin 12 on the M7261; nothing on the processor drives it.
- On the display board the SPDT common (pin 2, the centre pad of the MTS-102 footprint) is ground
  in all four switches, matching the W131.
- ERC: six warnings only (dangling no-connect flags; LVC245 inputs tied to ground). DRC: one
  dangling via on `ISR1_H`. The display board is clean.
- Firmware: builds without warnings, 3998 bytes flash, 14 bytes RAM.

## Errors found

### 1. `ALU_S` polarity wrong in the firmware

DEC marks ALU S3–S0 "D" on the KM-2 overlay: dim when asserted, so the pin carries the signal
active low, exactly like MPC. `km11-signals.txt` and the README both say so. But
`src/signals.cpp` had `invertMask[3] = 0x00` ("SPAD and ALU_S, straight") because `mapping.txt`
had lost the star on `ALU_S`. The screen showed the complement of the ALU function code.

### 2. MPC truncated on screen, and shown in the wrong base

`kmmain.cpp` formatted the 8-bit MPC as two *decimal* digits, so anything from 100 up lost its
hundreds digit: the console halt loop at 300₈ / 302₈ (192 / 194) displayed as "92" / "94". DEC's
flow charts and §5.10 quote the MPC in octal ("the MPC should read 321₈, which is the contents of
the NXT field of LOC 100₈"), and 8 bits need three octal digits. AMUX was hex; the console lights
and everything in the manual are octal.

### 3. "C2" is DEC's "C0"

Table 5-4: "BUS C1 and C0 together signify the type of Unibus cycle": C1 C0 = 00 DATI, 01 DATIP,
10 DATO, 11 DATOB. The signal was called C2 in `km11-signals.txt`, `mapping.txt`, `signals.h`, on
the schematic and on the screen. The wire (U7 P4) is right, only the name was wrong. Jörg's
overlay has the same slip, which is where it came from.

### 4. The fourth switch is AC LO, not "No Timeout"

On the 11/05, KM11 pin B2 is BUS AC LO: "When actuated in the direction of the arrow (ON), AC LO
asserts BUS AC LO as long as the switch is ON" (Table 5-4; drawing 5509081-0-9 says the same). The
TIME OUT meaning belongs to the 11/20's W131. The display board called it `SW_NoTimeout` with a
"No Timeout" silkscreen. Throwing it starts the power-fail sequence. Note also §5.9 b: with the
manual clock enabled, bus error timeouts are disabled anyway, so a "no timeout" switch would be
redundant on this machine. The net keeps its historical name `TIMEOUT_H`; the labels say AC LO.

## Minor

- `kmmain.cpp` and the `Makefile` said U1 runs at 3.3 V; U1's VCC is +5 V in the netlist. The
  /2 prescaler is still fine, the reasoning was stale.
- §5.9 e: the MPC lamps show the address of the *next* microstep, not the current one.
- `pcfStatus` is computed but never shown. An expander that stops answering reads 0xFF, so its
  active-low flags silently show as negated. Some indicator would help. *(open)*
- There is no I²C bus recovery. If the ATtiny resets mid-read — UPDI programming does that — a
  PCF8574 can hold SDA low and every transaction then times out until a power cycle. Nine SCL
  pulses before `twiInit()` would clear it. *(open)*
- M CLK PULSE takes two ON/OFF throws per processor clock (§5.10). A momentary (ON)-OFF toggle
  for SW1 would be kinder than the MTS-102 ON-ON. *(open)*

## Fixes applied

- `src/signals.cpp`: `invertMask[3]` is `0xf0`, ALU_S inverted; comments corrected.
- `src/kmmain.cpp`: MPC three octal digits, AMUX six octal digits (`formatOctal`); "C2" → "C0";
  the 3.3 V comment corrected.
- `src/signals.h`: `SIG_C2` → `SIG_C0`, comments.
- `src/Makefile`: the 3.3 V comment corrected.
- `mapping.txt`, `km11-signals.txt`: `C2*` → `C0*`, `ALU_S*` marked active low.
- `kicad/km1105/km1105.kicad_sch`: annotation text "km2: c2*" → "km2: c0*".
- `kicad/kmdisp/kmdisp.kicad_sch`, `kmdisp.kicad_pcb`: SW4 is `SW_AcLo`, silkscreen "AC LO".
- `README.md`, `CLAUDE.md`: C0, AC LO, octal, "next microstep" noted.
