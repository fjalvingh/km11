# KM11 display console

Tilted desk console for the KM11's remote display: the display board
(`kicad/kmdisp`) carrying a 1.8" 128x160 ST7735S SPI TFT mounted landscape and
four MTS-102 toggle switches, on a ribbon cable back to the card.

`console.py` is the parametric source (CadQuery). Everything is driven from the
PARAMETERS block at the top; change a number and re-run.

## Parts

| STL | Size | Print orientation |
|---|---|---|
| `console_panel.stl` | 118.4 x 56 x 3.2 mm | face on the bed |
| `console_base.stl` | 118.4 x 56 x 22.8 mm | floor on the bed, walls up |
| `console_pedestal.stl` | 86 x 73.5 x 33.3 mm | as exported, base on the bed |

`console_assembly.stl` is for looking at only — do not slice it.

Assembled: **118.4 x 73.5 x 56 mm**, panel tilted 30 degrees off horizontal with
the back edge raised.

The panel is a flat plate. The base is the floor plus all four walls, with a
screw stud in each corner; the panel screws down onto them.

## What fixes the geometry

The switch holes and the display window are where the display board puts the
switches and the LCD; they must not move. `console.py` holds the board's KiCad
coordinates (`kc_sw`, `kc_board`, `kc_j2`) and places the board by its switch
cluster, so the checks it prints (board to walls, board to studs) come from the
real layout. The board spans x = -2.39 .. 56.99 mm in box coordinates, past
where the old right wall stood, which is why the box grew 4.4 mm on the right.
The left edge did not move.

The board hangs off the panel by its switches. Nothing else touches it. The
studs sit in the corners, 1.3–1.5 mm clear of its front and back edges.

## The ribbon

J2 is on the board's panel side, so the IDC plug's back, where the cable
leaves it, is just under the panel. Crimp the plug so the cable leaves toward
the switches' side (+x). It then runs flat out through a 27 x 3 mm notch at
the top of the right wall, centred on J2 (y = 15 .. 42 mm). The panel closes
the notch, so the cable is laid in rather than threaded through.

## How the display mounts

The glass stands 2.25 mm proud of its PCB and drops into the window from behind.
The window is cut to the measured glass outline (43.7 x 34.05 + 0.3 mm per
side), so nothing overlaps the glass. The module sits on the display board's
socket, so how far into the window it reaches depends on the board and socket
heights, not on the console.

## Print settings

PLA, 0.2 mm layer, 3 walls, 20% gyroid infill, **no supports**, no brim.

In its export orientation, no face on any part overhangs by more than 45 degrees
from vertical. Only the panel's edge chamfer and screw countersinks reach 45
degrees, because they are on the bed side. Don't re-orient the parts in the
slicer. Use 3 walls rather than 2, because the switch bushings, the panel screws
and the pedestal screws all thread into printed plastic.

## Hardware

- 8 x M3 countersunk self-tapping screws: 4 x 12 mm (panel to base studs),
  4 x 10 mm (base to pedestal)
- 4 x MTS-102 toggle switches with their nuts and washers, on the display board

## Assembly

1. Offer the base up to the pedestal and drive four M3 x 10 from inside the
   base into the pedestal. They are countersunk, so the heads end flush with
   the floor under the display board.
2. Fit the display board to the panel: toggles through their holes with the
   nuts on the outside, LCD glass in the window.
3. Plug in the ribbon, cable leaving toward the switches.
4. Lay the ribbon in the notch in the right wall and drop the panel onto the
   base. Then drive the four M3 x 12 corner screws into the studs.

## Check before printing

- **Depth.** The cavity is 18.8 mm from the panel's underside to the floor.
  The board, socket and switch stack were not measured into the model. Check
  that the back of the display board and its solder joints clear the floor, and
  that the pedestal screw heads under it are flush. If not, raise `box_t`.
- **Notch height.** `cable_h` = 3 mm assumes the IDC plug's back comes within a
  couple of mm of the panel. If the plug sits lower, the ribbon has to rise to
  the notch. Deepen the notch if that bend is too sharp.

## Panel layout

Measured from the panel's front-left corner, looking at the panel:

- display window 44.30 x 34.65 mm, spanning x = 10.05 .. 54.35 mm, y = 10.68 .. 45.33 mm
- switch holes 6.4 mm dia at x = 82.5 / 98.5 mm, y = 20 / 36 mm
- corner screws 5.4 mm in from the left and right edges, at y = 5.5 / 50.5 mm

## Regenerating

```bash
cd 3d
.venv/bin/python run_cadquery_model.py console.py --preview --strict
```

`run_cadquery_model.py`, `preview.py` and `mesh_io.py` are the headless
render helpers; they write `console_*_preview.png` next to each STL.
