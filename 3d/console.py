"""
Tilted console for a 1.8" 128x160 ST7735S SPI TFT and four MTS-102 toggle
switches.

The box is a plain rectangular box - all faces at right angles. Its top face
carries the display and the switches. The whole box is tilted `tilt_deg` off
horizontal, back edge raised, and is held there by a pedestal that mounts to
the rear strip of the underside. The pedestal reaches forward into a foot so
the thing cannot tip.

The display is mounted landscape: the module's long axis runs across the panel.
Its glass stands 2.25 mm proud of its PCB, so the glass drops into the window
from behind and the PCB lies flat against the panel's inner face. Nothing
overlaps the glass, so no bezel dimension has to be guessed - the window is cut
to the measured glass outline and the picture sits wherever it sits inside it.
Four posts on the lid hold the module against the panel.

Three printed parts, each with a print orientation that needs no supports:

  SHELL     box body: top panel plus four walls, open underneath. Carries the
            display window, the four switch holes and the cable slot.
            Printed panel-face-down - flat panel on the bed, walls vertical.
  LID       flat plate closing the underside. Carries the four posts that clamp
            the display. Prints flat, posts upward.
  PEDESTAL  the stand. Mates against the lid's outer face over the rear strip.
            Printed base-down.

Coordinates: SHELL and LID are modelled in the BOX frame - x across the panel,
y from the front edge back, z down into the box from the panel face (z=0). The
pedestal body is modelled in world YZ; its cut features are modelled in the box
frame and mapped across with place().

Assembly order:
  1. drop the display into the window from inside, glass through the opening
  2. fit the four toggles, wire up, feed the ribbon out through the rear slot
  3. offer the pedestal up to the lid and drive four M3 from the lid's inner face
  4. drop the lid onto the shell and drive the four corner screws - those sit
     outside the pedestal's width and stay reachable
"""

import math

import cadquery as cq

# ============================================================
# PARAMETERS
# ============================================================
# Attitude
tilt_deg = 30.0        # deg - tilt of the whole box off horizontal, back edge up

# Box
box_w = 114.0          # mm - panel width (across, display left / switches right)
box_l = 56.0           # mm - panel depth (front edge to back edge)
box_t = 26.0           # mm - box thickness, panel face to underside
wall = 2.4             # mm - side wall thickness
panel_t = 3.2          # mm - top panel thickness (MTS-102 takes up to ~4mm)
lid_t = 4.0            # mm - underside lid thickness
lip_h = 1.5            # mm - locating lip on the lid
lip_clearance = 0.3    # mm - per-side clearance on that lip
corner_r = 3.0         # mm - rounding on the box's four upright corners
edge_cham = 0.8        # mm - chamfer around the panel face and the lid's outer face

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
disp_post_d = 4.0      # mm - clamping post diameter
disp_post_gap = 0.4    # mm - gap left under the posts, take up with a foam pad
# Clamp posts sit this far in from the PCB's edges, so they push against the
# strips of panel the PCB actually rests on rather than bowing it into the
# window. The +x pair lands on the header strip, clear of a centred 8-pin row;
# check the back of your module and move them if one hits a component.
disp_post_inset_x = 2.5   # mm
disp_post_inset_y = 3.5   # mm

# Switches: MTS-102, M6x0.75 bushing, 13 x 8 x 10 mm body behind the panel
sw_hole_d = 6.4        # mm - panel hole
sw_pitch = 16.0        # mm - 2 x 2 grid pitch, both axes
sw_cx = 33.5           # mm - cluster centre, x
sw_cy = 28.0           # mm - cluster centre, y

# Fasteners - M3 throughout
screw_d = 3.4          # mm - M3 clearance
screw_cb_d = 6.2       # mm - counterbore for an M3 socket head
screw_cb_h = 2.5       # mm - counterbore depth
boss_d = 6.5           # mm - screw boss outside diameter
boss_clr = 0.5         # mm - diametral clearance where the shell's boss passes through the lid's lip
pilot_d = 2.6          # mm - M3 self-tapping pilot
lid_screw_x = 51.6     # mm - shell screw positions, +/- x (boss overlaps the side wall)
lid_screw_y = (6.0, 50.0)      # mm - shell screw positions, y
ped_screw_x = 30.0     # mm - pedestal screw positions, +/- x
ped_screw_y = (42.0, 50.0)     # mm - pedestal screw positions, y
ped_pilot_depth = 8.0  # mm - how far the pilot goes into the pedestal

# Cable: 20-way 1.27mm ribbon is 25.4 mm wide. The slot is centred on the
# display, not the box, so the ribbon runs straight back off the module and
# clears the +x pair of clamp posts.
cable_w = 25.0         # mm - slot width
cable_h = 2.5          # mm - slot height
cable_x = disp_cx      # mm - slot centre, x
cable_z = -15.0        # mm - slot centre, box-frame z

# Pedestal
ped_w = 86.0           # mm - pedestal width (narrower than the box)
ped_band = 24.0        # mm - length of underside it grips, measured from the back
ped_front_ang = 50.0   # deg - front face of the pedestal, measured off the lid plane
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

shell_d = box_t - lid_t                      # how deep the shell reaches below the panel
cavity_d = shell_d - panel_t                 # usable depth behind the panel
lift = box_t * c + foot_t + ground_gap       # so the foot slides under the box's low corner


def to_world(y, z):
    """Box frame (y, z) -> world (Y, Z)."""
    return (y * c - z * s, y * s + z * c + lift)


def place(part):
    """Box frame -> world."""
    return part.rotate((0, 0, 0), (1, 0, 0), tilt_deg).translate((0, 0, lift))


BB = to_world(box_l, -box_t)     # back-bottom

lid_dir = (c, s)                 # along the underside, front -> back

lid_screw_pts = [(sx * lid_screw_x, y) for sx in (-1, 1) for y in lid_screw_y]
ped_screw_pts = [(sx * ped_screw_x, y) for sx in (-1, 1) for y in ped_screw_y]
disp_post_pts = [(disp_cx + sx * (disp_pcb_w / 2 - disp_post_inset_x),
                  disp_cy + sy * (disp_pcb_l / 2 - disp_post_inset_y))
                 for sx in (-1, 1) for sy in (-1, 1)]
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


# ============================================================
# SHELL
# ============================================================
shell = blk(-box_w / 2, box_w / 2, 0, box_l, -shell_d, 0, corner_r)
shell = shell.faces(">Z").edges().chamfer(edge_cham)
shell = shell.cut(blk(-box_w / 2 + wall, box_w / 2 - wall, wall, box_l - wall,
                      -shell_d, -panel_t, corner_r - wall + 0.6))

# screw bosses for the lid, merged into the side walls
shell = shell.union(posts(lid_screw_pts, boss_d, -shell_d, -panel_t))

# display window - the glass drops into it from behind, the PCB lies flat
# against the panel's inner face on either side of it
shell = shell.cut(blk(win_cx - win_w / 2, win_cx + win_w / 2,
                      disp_cy - win_l / 2, disp_cy + win_l / 2,
                      -panel_t - eps, eps))

# switches
shell = shell.cut(posts(sw_pts, sw_hole_d, -panel_t - eps, eps))

# lid screw pilots, drilled up from the open underside
shell = shell.cut(posts(lid_screw_pts, pilot_d, -shell_d - eps, -panel_t + 2.0))

# cable slot through the back wall
shell = shell.cut(blk(cable_x - cable_w / 2, cable_x + cable_w / 2, box_l - wall - eps, box_l + eps,
                      cable_z - cable_h / 2, cable_z + cable_h / 2))

# ============================================================
# LID
# ============================================================
lid = blk(-box_w / 2, box_w / 2, 0, box_l, -box_t, -shell_d, corner_r)
lid = lid.faces("<Z").edges().chamfer(edge_cham)
lid = lid.union(blk(-box_w / 2 + wall + lip_clearance, box_w / 2 - wall - lip_clearance,
                    wall + lip_clearance, box_l - wall - lip_clearance,
                    -shell_d, -shell_d + lip_h, 1.0))

# posts that clamp the display against the panel
disp_post_top = -(panel_t + disp_pcb_t) - disp_post_gap
lid = lid.union(posts(disp_post_pts, disp_post_d, -shell_d, disp_post_top))

# clearance for the screws that pull the pedestal on, driven from inside the box.
# The hole has to clear the lip as well as the plate, or it dead-ends at the
# lip's underside instead of reaching the inner face.
lid = lid.cut(posts(ped_screw_pts, screw_d, -box_t - eps, -shell_d + lip_h + eps))

# counterbored clearance holes for the shell screws - same full-depth hole
# through plate and lip, plus a wider pocket through the lip alone: the
# shell's screw bosses land in this exact footprint and are wider than the
# screw, so the lip needs to be relieved around the hole or the boss collides
# with it on assembly.
lid = lid.cut(posts(lid_screw_pts, screw_d, -box_t - eps, -shell_d + lip_h + eps))
lid = lid.cut(posts(lid_screw_pts, boss_d + boss_clr, -shell_d - eps, -shell_d + lip_h + eps))
lid = lid.cut(posts(lid_screw_pts, screw_cb_d, -box_t - eps, -box_t + screw_cb_h))

# ============================================================
# PEDESTAL
# ============================================================
Q = (BB[0] - ped_band * lid_dir[0], BB[1] - ped_band * lid_dir[1])

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


pedestal = ped_extrusion(ped_w).translate((-ped_w / 2.0, 0, 0))
pedestal = pedestal.edges("|X").fillet(ped_edge_r)
pedestal = pedestal.cut(
    ped_extrusion(ped_w - 2 * ped_wall, -ped_wall)
    .translate((-ped_w / 2.0 + ped_wall, 0, -ped_wall)))

# pilot holes for the screws coming through the lid
pedestal = pedestal.cut(place(posts(ped_screw_pts, pilot_d,
                                    -box_t - ped_pilot_depth, -box_t + eps)))

# ============================================================
# EXPORT
# ============================================================
assembly = place(shell).union(place(lid)).union(pedestal)

shell_print = shell.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, box_l, 0))
lid_print = lid.translate((0, 0, box_t))     # outer face on the bed, posts upward

for shape, name in ((assembly, "console_assembly"),
                    (shell_print, "console_shell"),
                    (lid_print, "console_lid"),
                    (pedestal, "console_pedestal")):
    cq.exporters.export(shape, f"{name}.stl", tolerance=0.01, angularTolerance=0.1)
    bb = shape.val().BoundingBox()
    print(f"{name:18s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:6.1f} mm  (z from {bb.zmin:.1f})")

print(f"panel {box_w} x {box_l} mm, cavity {cavity_d:.1f} mm deep behind the panel")
print(f"display PCB x {disp_cx - disp_pcb_w / 2:.2f}..{disp_cx + disp_pcb_w / 2:.2f}, "
      f"y {disp_cy - disp_pcb_l / 2:.2f}..{disp_cy + disp_pcb_l / 2:.2f}")
print(f"window {win_w:.2f} x {win_l:.2f} at x {win_cx - win_w / 2:.2f}..{win_cx + win_w / 2:.2f}")
print(f"  PCB shoulder behind the panel: "
      f"{(win_cx - win_w / 2) - (disp_cx - disp_pcb_w / 2):.2f} mm at -x, "
      f"{(disp_cx + disp_pcb_w / 2) - (win_cx + win_w / 2):.2f} mm at +x")
print(f"glass sits {panel_t - disp_glass_h:.2f} mm below the panel face")
print(f"switch holes d{sw_hole_d} at x={sw_pts[0][0]}/{sw_pts[2][0]}, "
      f"y={sw_pts[0][1]}/{sw_pts[1][1]}")
print(f"gap PCB to nearest switch nut: "
      f"{sw_pts[0][0] - 5.775 - (disp_cx + disp_pcb_w / 2):.2f} mm")
print(f"display clamp posts {disp_post_top + shell_d:.1f} mm tall")
print(f"cable slot {cable_w} x {cable_h} at x {cable_x - cable_w / 2:.2f}..{cable_x + cable_w / 2:.2f}, "
      f"posts at x={disp_post_pts[0][0]:.2f}/{disp_post_pts[2][0]:.2f}")
