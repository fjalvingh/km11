# KM11 display console

Tilted desk console for the KM11's remote display: a 1.8" 128x160 ST7735S SPI TFT
mounted landscape, and four MTS-102 toggle switches, on a ribbon cable back to
the board.

`console.py` is the parametric source (CadQuery). Everything is driven from the
PARAMETERS block at the top; change a number and re-run.

## Parts

| STL | Size | Print orientation |
|---|---|---|
| `console_shell.stl` | 114 x 56 x 22 mm | panel face flat on the bed, opening up |
| `console_lid.stl` | 114 x 56 x 20.8 mm | outer face on the bed, posts up |
| `console_pedestal.stl` | 86 x 74 x 58 mm | as exported, base on the bed |

`console_assembly.stl` is for looking at only — do not slice it.

Assembled: **114 x 74 x 58 mm**, panel tilted 30 degrees off horizontal with the
back edge raised.

## How the display mounts

The glass stands 2.25 mm proud of its PCB, so the module goes in from behind and
the glass drops through the window; the PCB then lies flat against the panel's
inner face. Nothing overlaps the glass, so no bezel dimension has to be guessed —
the window is cut to the measured glass outline (43.7 x 34.05 + 0.3 mm per side)
and the picture sits wherever it sits inside it. The glass ends up 0.95 mm below
the panel face, so it is protected but not shadowed.

Four posts on the lid press the PCB against the panel. They stop 0.4 mm short on
purpose — put a 1-2 mm foam pad on the back of the module and the lid clamps it.

## Print settings

PLA, 0.2 mm layer, 3 walls, 20% gyroid infill, **no supports**, no brim.

Every face on every part is vertical, upward-facing, or overhangs by less than
45 degrees from vertical in its export orientation — that is the whole reason
the enclosure is split this way, so don't re-orient the parts in the slicer.
3 walls rather than 2 because the switch bushings, the lid screws and the
pedestal screws all thread into printed plastic.

## Hardware

- 8 x M3 self-tapping screws: 4 x 12 mm (lid to shell), 4 x 10 mm (lid to pedestal)
- 4 x MTS-102 toggle switches with their nuts and washers
- a scrap of foam, 1-2 mm, for the back of the display

## Assembly

1. Drop the display into the window from inside, glass through the opening,
   pin header toward the **switches** (+x).
2. Fit the four toggles, nuts on the outside.
3. Wire up. Feed the ribbon out through the slot in the back wall.
4. Lay the foam pad over the back of the display PCB.
5. Offer the pedestal up to the lid's outer face and drive four M3 x 10 from the
   lid's inner face into the pedestal.
6. Drop the lid onto the shell, ribbon fed through the pedestal's upstand slot,
   and drive the four M3 x 12 corner screws. Those four sit outside the
   pedestal's 86 mm width, so they stay reachable.

## Check before printing

**`disp_post_inset_x` / `disp_post_inset_y`.** The clamp posts land on the back of
the module at (-47.95, 14.45), (-47.95, 41.55), (2.95, 14.45), (2.95, 41.55) in
panel coordinates — 2.5 mm in from the PCB's short edges, 3.5 mm in from the long
ones. The +x pair sits on the 8.4 mm header strip, clear of a centred 8-pin row.
Check the back of your board and move them if one would land on a component.

**`disp_pcb_t` = 1.6 mm** is assumed, not measured. It only sets the clamp post
height, and the foam pad absorbs the error.

## Panel layout

Measured from the panel's front-left corner, looking at the panel:

- display window 44.30 x 34.65 mm, spanning x = 10.05 .. 54.35 mm, y = 10.68 .. 45.33 mm
- display PCB behind it spans x = 6.55 .. 62.45 mm — 3.5 mm of overlap at the left
  end, 8.1 mm at the header end
- switch holes 6.4 mm dia at x = 82.5 / 98.5 mm, y = 20 / 36 mm
- 14.3 mm clear between the PCB edge and the nearest switch nut

The glass sits 3.8 mm in from the PCB's left edge, which puts `disp_glass_off` at
-2.30 mm. That is derived from the 3.8 mm in `console.py`, so if you re-measure,
change the 3.8 and the window follows.

## Regenerating

```bash
cd 3d
.venv/bin/python run_cadquery_model.py console.py --preview --strict
```

`run_cadquery_model.py`, `preview.py` and `mesh_io.py` are the headless
render helpers; they write `console_*_preview.png` next to each STL.
