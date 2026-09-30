"""
Tilted console for a 1.8" 128x160 ST7735S SPI TFT and four MTS-102 toggle
switches.

The box is a plain rectangular box - all faces at right angles. Its top face
carries the display and the switches. The whole box is tilted `tilt_deg` off
horizontal, back edge raised, and is held there by a pedestal that mounts to
the rear strip of the underside. The pedestal reaches forward into a foot so
the thing cannot tip.

The switches and the display sit on the display board (kicad/kmdisp): the
toggles are soldered to it and screwed through the panel, the LCD module plugs
into its socket J1. The board hangs off the panel by its switches and touches
nothing else. The display is landscape, its glass dropping into the window
from behind; the window is cut to the measured glass outline. Switch and window
positions are fixed by that board - do not move them.

The ribbon plugs into the board's J2 from the panel side, so the IDC plug's
back, where the cable leaves, is just under the panel. The cable runs out
sideways through a notch at the top of the right-hand (+x) wall, which the
panel closes. The box is wider on that side than on the other to make room for
the board edge and the cable.

Three printed parts, each with a print orientation that needs no supports:

  PANEL     flat top plate. Carries the display window, the four switch holes
            and four countersunk screw holes. Printed face-down.
  BASE      floor plus four walls, open on top, with a screw stud in each corner
            and the cable notch in the right wall. Printed floor-down.
  PEDESTAL  the stand. Mates against the base's floor over the rear strip.
            Printed base-down.

Coordinates: PANEL and BASE are modelled in the BOX frame - x across the panel,
y from the front edge back, z down into the box from the panel face (z=0). The
pedestal body is modelled in world YZ; its cut features are modelled in the box
frame and mapped across with place().

Assembly order:
  1. offer the base up to the pedestal and drive four countersunk M3 from inside
     the base - their heads end flush with the floor, under the display board
  2. fit the display board to the panel: toggles through their holes, nuts on
     the outside, LCD glass in the window. Plug in the ribbon.
  3. lay the ribbon in the notch, drop the panel onto the base and drive the
     four countersunk corner screws into the studs
"""

import math

import cadquery as cq

# ============================================================
# PARAMETERS
# ============================================================
# Attitude
tilt_deg = 30.0        # deg - tilt of the whole box off horizontal, back edge up

# Box. The left edge is where it always was; the right edge follows the display
# board, see box_x1 below.
box_x0 = -57.0         # mm - left (-x) outer edge
box_l = 56.0           # mm - panel depth (front edge to back edge)
box_t = 26.0           # mm - box thickness, panel face to underside
wall = 2.4             # mm - side wall thickness
panel_t = 3.2          # mm - top panel thickness (MTS-102 takes up to ~4mm)
floor_t = 4.0          # mm - base floor thickness
corner_r = 3.0         # mm - rounding on the box's four upright corners
edge_cham = 0.8        # mm - chamfer around the panel face and the base's outer face

# Display: 1.8" 128x160 SPI TFT, ST7735S, mounted landscape.
disp_pcb_w = 55.9      # mm - PCB, across the panel
disp_pcb_l = 34.1      # mm - PCB, front to back
disp_pcb_t = 1.6       # mm - PCB thickness (assumed, not measured)
disp_glass_w = 43.7    # mm - glass, across the panel
disp_glass_l = 34.05   # mm - glass, front to back
disp_glass_h = 2.25    # mm - how far the glass stands above the PCB
# Where the glass sits along the PCB, as an offset of the glass centre from the
# PCB centre. Negative shifts the glass toward -x, which puts the pin header at
# the +x end, between the display and the switches. Measured: the glass starts
# 3.8 mm in from the -x edge of the PCB, leaving an 8.4 mm header strip at +x.
disp_glass_off = -(disp_pcb_w / 2 - 3.8 - disp_glass_w / 2)   # mm -> -2.30
disp_cx = -22.5        # mm - PCB centre, x
disp_cy = 28.0         # mm - PCB centre, y
win_clr = 0.3          # mm - clearance around the glass in the window, per side

# Switches: MTS-102, M6x0.75 bushing, 13 x 8 x 10 mm body behind the panel
sw_hole_d = 6.4        # mm - panel hole
sw_pitch = 16.0        # mm - 2 x 2 grid pitch, both axes
sw_cx = 33.5           # mm - cluster centre, x
sw_cy = 28.0           # mm - cluster centre, y

# Display board (kicad/kmdisp/kmdisp.kicad_pcb), in KiCad coordinates, tied to
# the box by the switch cluster. The board faces the panel with its component
# side, so KiCad x maps straight onto box x and KiCad y (downward on screen)
# runs toward the front edge.
kc_sw = (108.0, 108.0)             # centre of SW1..SW4
kc_board = (72.11, 90.01, 131.49, 125.76)   # Edge.Cuts rectangle x0, y0, x1, y1
kc_j2 = (126.035, 96.065)          # J2 pin 1; 2x10, rows +x, pins run +y
board_x0 = kc_board[0] - kc_sw[0] + sw_cx
board_x1 = kc_board[2] - kc_sw[0] + sw_cx
board_y0 = sw_cy - (kc_board[3] - kc_sw[1])
board_y1 = sw_cy - (kc_board[1] - kc_sw[1])
j2_x = kc_j2[0] + 1.27 - kc_sw[0] + sw_cx
j2_y = sw_cy - (kc_j2[1] + 4.5 * 2.54 - kc_sw[1])
board_clr = 2.0        # mm - board edge to the inside of the right wall

# Fasteners - M3 countersunk (90 degree) throughout
screw_d = 3.4          # mm - M3 clearance
csk_d = 6.4            # mm - countersink diameter at the surface, M3 head is 6.0
boss_d = 6.5           # mm - screw stud outside diameter
pilot_d = 2.6          # mm - M3 self-tapping pilot
stud_inset_x = 5.4     # mm - panel screw centres in from the left/right outer edges
stud_y = (5.5, 50.5)   # mm - panel screw positions, y; clear of the board's front/back edges
ped_screw_x = 30.0     # mm - pedestal screw positions, +/- x from the box centre
ped_screw_y = (42.0, 50.0)     # mm - pedestal screw positions, y
ped_pilot_depth = 8.0  # mm - how far the pilot goes into the pedestal

# Cable: 20-way 1.27 mm ribbon, 25.4 mm wide, leaving the IDC plug on J2
# sideways toward +x. It goes through a notch in the top of the right wall,
# centred on J2, and the panel closes the notch over it.
cable_w = 27.0         # mm - notch width
cable_h = 3.0          # mm - notch depth below the panel's underside
cable_y = j2_y         # mm - notch centre, y

# Pedestal
ped_w = 86.0           # mm - pedestal width (narrower than the box)
ped_band = 24.0        # mm - length of underside it grips, measured from the back
ped_front_ang = 50.0   # deg - front face of the pedestal, measured off the floor plane
ped_wall = 6.0         # mm - pedestal shell thickness (hollow, open underneath)
ped_edge_r = 1.5       # mm - rounding along the pedestal's profile edges
foot_t = 4.0           # mm - thickness of the foot plate
foot_front = -2.0      # mm - world Y the foot reaches to, in front of the panel edge
foot_rear = 72.0       # mm - world Y the base reaches to, behind the box
ground_gap = 2.0       # mm - clearance under the box's lowest corner, over the foot

eps = 0.01

# ============================================================
# FRAMES
# ============================================================
c = math.cos(math.radians(tilt_deg))
s = math.sin(math.radians(tilt_deg))

box_x1 = board_x1 + board_clr + wall        # right (+x) outer edge
box_w = box_x1 - box_x0
box_xc = (box_x0 + box_x1) / 2.0
cavity_d = box_t - panel_t - floor_t         # usable depth behind the panel
lift = box_t * c + foot_t + ground_gap       # so the foot slides under the box's low corner


def to_world(y, z):
    """Box frame (y, z) -> world (Y, Z)."""
    return (y * c - z * s, y * s + z * c + lift)


def place(part):
    """Box frame -> world."""
    return part.rotate((0, 0, 0), (1, 0, 0), tilt_deg).translate((0, 0, lift))


BB = to_world(box_l, -box_t)     # back-bottom

under_dir = (c, s)               # along the underside, front -> back

stud_pts = [(x, y) for x in (box_x0 + stud_inset_x, box_x1 - stud_inset_x) for y in stud_y]
ped_screw_pts = [(box_xc + sx * ped_screw_x, y) for sx in (-1, 1) for y in ped_screw_y]
sw_pts = [(sw_cx + sx * sw_pitch / 2.0, sw_cy + sy * sw_pitch / 2.0)
          for sx in (-1, 1) for sy in (-1, 1)]

win_w = disp_glass_w + 2 * win_clr
win_l = disp_glass_l + 2 * win_clr
win_cx = disp_cx + disp_glass_off


def blk(x0, x1, y0, y1, z0, z1, fillet_r=None):
    """Axis-aligned box in the box frame, corner to corner, optionally with
    rounded upright edges."""
    w = (cq.Workplane("XY")
         .box(x1 - x0, y1 - y0, z1 - z0, centered=False)
         .translate((x0, y0, z0)))
    return w.edges("|Z").fillet(fillet_r) if fillet_r else w


def posts(points, diameter, z0, z1):
    """Vertical cylinders in the box frame at the given (x, y) points."""
    return (cq.Workplane("XY", origin=(0, 0, z0))
            .pushPoints(points)
            .circle(diameter / 2.0)
            .extrude(z1 - z0))


def csk(points, z_face, down):
    """90 degree countersinks at the given (x, y) points, opening at z_face and
    narrowing into the part - downward (-z) if `down`, else upward."""
    h = (csk_d - screw_d) / 2.0
    d = -1 if down else 1
    cones = [cq.Solid.makeCone(csk_d / 2.0 + eps, screw_d / 2.0, h + eps,
                               cq.Vector(x, y, z_face - d * eps), cq.Vector(0, 0, d))
             for x, y in points]
    return cq.Workplane("XY").newObject(cones).combine()


# ============================================================
# PANEL
# ============================================================
panel = blk(box_x0, box_x1, 0, box_l, -panel_t, 0, corner_r)
panel = panel.faces(">Z").edges().chamfer(edge_cham)

# display window - the glass drops into it from behind
panel = panel.cut(blk(win_cx - win_w / 2, win_cx + win_w / 2,
                      disp_cy - win_l / 2, disp_cy + win_l / 2,
                      -panel_t - eps, eps))

# switches
panel = panel.cut(posts(sw_pts, sw_hole_d, -panel_t - eps, eps))

# countersunk screws into the base's studs
panel = panel.cut(posts(stud_pts, screw_d, -panel_t - eps, eps))
panel = panel.cut(csk(stud_pts, 0, down=True))

# ============================================================
# BASE
# ============================================================
base = blk(box_x0, box_x1, 0, box_l, -box_t, -panel_t, corner_r)
base = base.faces("<Z").edges().chamfer(edge_cham)
base = base.cut(blk(box_x0 + wall, box_x1 - wall, wall, box_l - wall,
                    -box_t + floor_t, -panel_t + eps, corner_r - wall + 0.6))

# screw studs, merged into the corners, with pilots from the top
base = base.union(posts(stud_pts, boss_d, -box_t + floor_t - eps, -panel_t))
base = base.cut(posts(stud_pts, pilot_d, -box_t + floor_t, -panel_t + eps))

# countersunk clearance for the screws that pull the pedestal on, driven from
# inside; the heads end flush with the floor, under the display board
base = base.cut(posts(ped_screw_pts, screw_d, -box_t - eps, -box_t + floor_t + eps))
base = base.cut(csk(ped_screw_pts, -box_t + floor_t, down=True))

# cable notch in the top of the right wall
base = base.cut(blk(box_x1 - wall - eps, box_x1 + eps,
                    cable_y - cable_w / 2, cable_y + cable_w / 2,
                    -panel_t - cable_h, -panel_t + eps))

# ============================================================
# PEDESTAL
# ============================================================
Q = (BB[0] - ped_band * under_dir[0], BB[1] - ped_band * under_dir[1])

_ang = math.radians(tilt_deg + ped_front_ang)
F1 = (Q[0] - (Q[1] - foot_t) / math.tan(_ang), foot_t)
F2 = (foot_front, foot_t)
F3 = (foot_front, 0.0)
PB = (foot_rear, 0.0)

ped_profile = [F3, PB, BB, Q, F1, F2]


def ped_extrusion(width, offset_2d=None):
    w = cq.Workplane("YZ").polyline(ped_profile).close()
    if offset_2d:
        w = w.offset2D(offset_2d)
    return w.extrude(width)


pedestal = ped_extrusion(ped_w).translate((box_xc - ped_w / 2.0, 0, 0))
pedestal = pedestal.edges("|X").fillet(ped_edge_r)
pedestal = pedestal.cut(
    ped_extrusion(ped_w - 2 * ped_wall, -ped_wall)
    .translate((box_xc - ped_w / 2.0 + ped_wall, 0, -ped_wall)))

# pilot holes for the screws coming through the floor
pedestal = pedestal.cut(place(posts(ped_screw_pts, pilot_d,
                                    -box_t - ped_pilot_depth, -box_t + eps)))

# ============================================================
# EXPORT
# ============================================================
assembly = place(panel).union(place(base)).union(pedestal)

panel_print = panel.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, box_l, 0))   # face on the bed
base_print = base.translate((0, 0, box_t))     # floor on the bed, walls upward

for shape, name in ((assembly, "console_assembly"),
                    (panel_print, "console_panel"),
                    (base_print, "console_base"),
                    (pedestal, "console_pedestal")):
    cq.exporters.export(shape, f"{name}.stl", tolerance=0.01, angularTolerance=0.1)
    bb = shape.val().BoundingBox()
    print(f"{name:18s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:6.1f} mm  (z from {bb.zmin:.1f})")

print(f"box x {box_x0:.2f}..{box_x1:.2f} ({box_w:.2f} x {box_l} mm), "
      f"cavity {cavity_d:.1f} mm deep behind the panel")
print(f"display PCB x {disp_cx - disp_pcb_w / 2:.2f}..{disp_cx + disp_pcb_w / 2:.2f}, "
      f"y {disp_cy - disp_pcb_l / 2:.2f}..{disp_cy + disp_pcb_l / 2:.2f}")
print(f"window {win_w:.2f} x {win_l:.2f} at x {win_cx - win_w / 2:.2f}..{win_cx + win_w / 2:.2f}")
print(f"switch holes d{sw_hole_d} at x={sw_pts[0][0]}/{sw_pts[2][0]}, "
      f"y={sw_pts[0][1]}/{sw_pts[1][1]}")
print(f"display board x {board_x0:.2f}..{board_x1:.2f}, y {board_y0:.2f}..{board_y1:.2f}, "
      f"J2 centre ({j2_x:.2f}, {j2_y:.2f})")
print(f"  board to inner walls: {board_x0 - (box_x0 + wall):.2f} left, "
      f"{(box_x1 - wall) - board_x1:.2f} right, "
      f"{board_y0 - wall:.2f} front, {(box_l - wall) - board_y1:.2f} back")
print(f"  board to studs: {board_y0 - (stud_y[0] + boss_d / 2):.2f} front, "
      f"{(stud_y[1] - boss_d / 2) - board_y1:.2f} back")
print(f"cable notch {cable_w} x {cable_h} in the right wall, y {cable_y - cable_w / 2:.2f}..{cable_y + cable_w / 2:.2f}")
print(f"pedestal x {box_xc - ped_w / 2:.2f}..{box_xc + ped_w / 2:.2f}")
