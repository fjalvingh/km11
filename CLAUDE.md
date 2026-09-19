# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A KiCad 10 design for a PDP-11 KM11 maintenance module with an LCD readout, plus the ATtiny firmware
that drives it in `src/`. Most of the repo is the hardware design; there are no tests.

The board is a **double-height, extended-length DEC flip-chip card** that plugs into *both* KM11 slots
of a PDP-11/05 or 11/10. It reads the backplane signals with six PCF8574T I2C port expanders, an
ATtiny1616 (possibly a 3216 if the code outgrows it) drives a 2.25" 76x284 SPI colour LCD which sits
outside the machine on a ribbon cable, together with the switches.

## Layout

- `kicad/km1105.kicad_sch` / `km1105.kicad_pcb` / `km1105.kicad_pro` — the live design (KiCad 10,
  file format 20260206). Single flat sheet, 2-layer board.
- `kicad/km1105.sch`, `km1105.pro`, `km1105-cache.lib`, `km1105-rescue.*` — leftovers from the legacy
  (EESchema v4) *DEC FLIP CHIP DOUBLE-HEIGHT EXTENDED-LENGTH* template this project was seeded from.
  They are not the working files; do not edit them to change the design. `sym-lib-table` still points
  at the template's cache lib under `/home/jal/hobby/git/kicad/dec-flip-chip-templates/`, which is
  where the `DEC-FLIP-CHIP:CONN-DEC-DOUBLE` edge-connector symbol/footprint comes from — that path is
  outside this repo and must exist for the schematic to load cleanly.
- `kicad/production/` — generated fabrication output (`bom.csv`, `positions.csv`, `designators.csv`,
  `netlist.ipc`, gerber zip), produced by the KiCad *Fabrication Toolkit* plugin (JLCPCB-style);
  settings in `kicad/fabrication-toolkit-options.json`. Regenerate rather than hand-edit.
- `kicad/km1105-backups/` — KiCad's own timestamped autobackups. Noise; ignore them when searching.
- `km11-signals.txt` — **the authoritative signal map**. Maps each front-panel LED (D1..D28) to its
  signal name and to the corresponding pin function in KM slot 1 and slot 2. Any question of the form
  "what does this net connect to on the backplane" is answered here first.

## Firmware (`src/`)

Bare-metal C++ for the ATtiny1616 (16K flash, **2K SRAM**), built with `avr-g++` via the Makefile:

```bash
cd src
make                     # compile + link + avr-size
make DIAG_MODE=1 upload  # avrdude over serialupdi; see the diagnostic ladder below
make fuse                # one-time: fuse2=0x7e for the 20MHz oscillator
```

`avr-size` output is the budget check — watch `data + bss` against 2048 bytes. The design rule that
follows from that: **no frame buffer, ever.** A 128x160 RGB565 frame is 40K, a 76x284 one 43K. The
driver (`lcd.cpp`) streams pixels out of SPI0 as it computes them and draws text one glyph at a time,
each glyph opening its own window. Fonts live in flash and are read with `pgm_read_byte`, described by
a `Font` struct (`font.h`): `FontSmall` is the 5x7 in a 6x8 cell, `FontLarge` an 8x16 converted from
Terminus. The drawing calls take a `const Font *` (defaulting to `FontSmall`) plus an integer `scale`.
Keep any new drawing code to that pattern, and use `PSTR`/`lcdDrawText_P` for fixed strings so
literals stay out of RAM. `const` tables do *not* need `PROGMEM` on this core — avr-gcc maps `.rodata`
into flash for `__AVR_ARCH__ 103` — but the font uses it harmlessly.

`clockInit()` sets the main clock prescaler to /2, so the core runs at 10MHz; `CLOCK` in the Makefile
is that post-prescaler speed. Leave it: nothing here needs 20MHz and the SPI dividers are tuned to 10.

**Supply levels: the schematic has U1 on +5V** (netlist, `U1.1 -> +5V`) with only the display supply
at J2.15/17 on U9's 3.3V, so the five display lines leave the ATtiny at 5V and hit a 3.3V controller
with nothing in between. That mismatch is real and the next board spin should carry a level shifter
(74LVC245/125 on the 3.3V rail) - the bare 76x284 panel will not tolerate 5V at all. But it was **not**
the cause of the September 2026 display failure, and neither was the ribbon: after two days of chasing
white-outs, crosstalk and series resistors, the fault was the 128x160 module itself, which had tested
fine in July and had degraded since. A second module worked at once. The symptom of a marginal panel
is that it *responds* to edge rate, supply voltage and resistors, so it looks exactly like a signal
integrity problem. Swap the panel before believing any of that. As of September 2026 the board runs
exactly this way - U1 and the expanders at 5V, the 128x160 module at 3.3V, straight through the
ribbon with no resistors - and a healthy module is stable on it at 625kHz.

`lcd_config.h` holds everything hardware-dependent. `LCD_CONTROLLER` picks between two panels, each
with its own geometry, offsets, colour order and inversion:

- `LCD_ST7789` — the M35-2.25TFT-lanban 76x284 ST7789P3 the KM11 board is designed around. **Never
  yet produced an image**; its offsets (82/18) and `LCD_VCOM` are unverified estimates.
- `LCD_ST7735` — a 128x160 1.8" board used for bring-up. Working.

### Bringing up a display

`DIAG_MODE` (a Makefile variable, not a source edit) is a ladder, each rung dropping more assumptions
than the last. Reach for it before theorising:

| Mode | What it does | What it proves |
|---|---|---|
| 0 | the real text screen | everything |
| 1 | flat fills and stripes via the normal path | geometry, offsets, rotation |
| 4 | floods raw frame memory, ignoring panel size | the controller is listening at all |
| 3 | reads the controller ID back over SDA, reports it on PB5 | traffic goes both ways |
| 2 | wiggles each pin at its own frequency, no SPI | the wiring, end to end |

Reading the symptoms: colours arriving as their exact complements means `LCD_INVERT` is wrong.
Unwritten bands whose width matches an offset mean the panel geometry is wrong. A screen that stays
plain white while the signals look perfect is usually VCOM or an incomplete init, not the link.

Other command-line knobs, all rebuild automatically via `diagmode.stamp`:

- `make LCD_SPI_DIV=128 upload` — SPI divider 4/16/64/128 (2.5MHz/625kHz/156kHz/78kHz). Default 16.
- `make EXTRA="-DLOOP_DELAY_MS=1000 -DNO_I2C" upload` — stretch the idle gap between frames and/or
  leave the expanders untouched, to separate what the bus does from what the drawing does.
- `make EXTRA=-DLCD_OPEN_DRAIN=1 upload` — bit-bang every display line open-drain (never driving a
  high, ~50kHz). Slow, gentle edges at reduced level: a panel that only works in this mode is either
  marginal itself (most likely, see above) or genuinely suffering from the level mismatch.
- `make EXTRA=-DLCD_DC_UNDER_CS=1 upload` — move the DC edges to moments when CS is high, so a glitch
  coupled from DC into SCK cannot count as a clock. For "first pixels after each command are wrong".
- `RESET_DIAG` (default 1) prints `Rxx Bnn Fnnn` bottom-right in mode 0: last reset cause from
  `RSTCTRL.RSTFR`, a `.noinit` boot counter, and a frame counter. `R21 B01` with `F` counting is a
  healthy MCU; a rising `B` means it is restarting, `F` frozen means it is hung.

**Before a long debugging session, try a second physical panel. Then try a third.** These cheap
modules are of variable quality and they degrade: one 128x160 board wasted a session with pixels it
never wrote; another tested fine in July 2026 and cost two days in September behaving like a cable
problem. In both cases an identical replacement worked immediately with no firmware change. Run the
ladder above only after a known-good panel has failed on the same wiring.

## Netlist conventions

- The card occupies two slots, so signals exist twice. The second slot's copy is the same net name with
  a `_2` suffix (`BSR0_H` / `BSR0_H_2`). Keep that convention when adding nets.
- `*_H` / `*_L` are DEC's active-high / active-low suffixes; `*` in `km11-signals.txt` is DEC's
  active-low marker for the backplane pin names.
- Main parts: `U1` ATtiny1616-S, `U2`–`U7` PCF8574T expanders, `U8` 74ACT00, `U9` LD1117 3V3 LDO,
  `J1` DEC double edge connector, `J2` display/switch header (2x10), `J3` UPDI programming header.

## Working with the design

`kicad-cli` (10.0.5) is on PATH; prefer it over hand-parsing the s-expression files:

```bash
cd kicad
kicad-cli sch erc km1105.kicad_sch          # electrical rules check
kicad-cli pcb drc km1105.kicad_pcb          # design rules check
kicad-cli sch export netlist km1105.kicad_sch -o /tmp/km1105.net
kicad-cli sch export pdf km1105.kicad_sch -o /tmp/km1105.pdf
kicad-cli pcb export gerbers --output /tmp/gerb km1105.kicad_pcb
```

The `konnect` MCP server (a KiCad plugin, configured in `.mcp.json`) can drive a running KiCad
instance; use it for interactive edits rather than editing `.kicad_pcb` / `.kicad_sch` text when it is
available.

Editing the s-expression files by hand is possible but risky: schematic and PCB hold *separate* copies
of reference designators and footprints, so a change on one side must be mirrored on the other or KiCad
reports duplicate/orphan references. If you do edit by hand, verify with `kicad-cli ... erc/drc`
afterwards.

Board files are huge (~20k lines PCB, ~17k lines schematic) — grep for the specific reference or net
rather than reading them whole.
