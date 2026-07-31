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
make            # compile + link + avr-size
make upload     # avrdude over serialupdi on /dev/ttyUSB0
make fuse       # one-time: fuse2=0x7e for the 20MHz oscillator
```

`avr-size` output is the budget check — watch `data + bss` against 2048 bytes. The design rule that
follows from that: **no frame buffer, ever.** A 76x284 RGB565 frame is 43K. The ST7789 driver
(`st7789.cpp`) streams pixels out of SPI0 as it computes them and draws text one glyph at a time, each
glyph opening its own 6x8 window; the 5x7 font lives in flash and is read with `pgm_read_byte`. Keep
any new drawing code to that pattern, and use `PSTR`/`lcdDrawText_P` for fixed strings so literals stay
out of RAM.

`lcd_config.h` holds every hardware-dependent number (pins, panel size, RAM offsets, rotation). The
`LCD_COL_OFFSET` / `LCD_ROW_OFFSET` pair is the usual suspect when the image is shifted or wrapped —
the ST7789 has 240x320 of memory and the 76x284 panel is a window inside it.

Note that the 20MHz fuse only picks the oscillator: `clockInit()` in `kmmain.cpp` clears
`CLKCTRL.MCLKCTRLB` to switch off the reset-default divide-by-6, without which F_CPU, `_delay_ms` and
the SPI clock are all six times off.

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
