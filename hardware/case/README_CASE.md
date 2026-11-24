# Kasper Backpack desktop case (rev A)

Desktop wedge enclosure for the Waveshare ESP32-S3-Touch-LCD-4.3B plus the Kasper Backpack (REV-9 board STEP).
Outside size: 145 W × 99 D × 92 H mm, plus 4 mm feet. The screen is tilted back 30°.

## Parts
| File | Qty | Print |
|---|---|---|
| Kasper_Case_Shell.stl/.step | 1 | Print **upside down on its flat top**. Supports only under the 4 screen-clamp bosses. 0.2 mm layers, 3 walls, 20 % infill |
| Kasper_Case_Base.stl/.step | 1 | Print flat with the feet facing up (feet side is the top as printed) or down with supports; standoffs face up |
| Kasper_Case_Clamp_x4.stl/.step | 4 | Print flat, pad facing up. Mirror 2 of them in the slicer (or flip them over; they are symmetric enough) |
| Kasper_Case_Assembly.step | n/a | Reference only: shell, base, clamps, Backpack, Waveshare dummy, 40×50×10 battery |
| kasper_case.py | n/a | CadQuery source; every dimension is a parameter at the top |

## Hardware
- 14 × M3 heat-set inserts (Ø4.2 hole, 6 mm long): 4 Backpack standoffs, 4 clamp bosses, 6 base bosses
- 4 × M3×6 pan head screws (Backpack) + 4 washers (the board holes are 4.4 mm)
- 4 × M3×6 screws (clamps)
- 6 × M3×8 flat (countersunk) screws (base → shell)
- KCD1-type snap-in rocker switch (cut-out 13 × 19.3 mm), wired to U7
- Optional: 3 mm light pipes for the LEDs, 4 × Ø10 mm rubber feet in the foot recesses
- Right-angle USB-C to USB-C cable (~15 cm) from the Backpack's rear USB-C (USBC2) to the Waveshare's USB-C (on the screen's right-hand side)
- Dupont jumpers from the Waveshare terminal block to H8–H11

## Assembly
1. Melt the inserts in.
2. Zip-tie the battery under the Backpack (max about 40 × 50 × 10 mm, between the standoffs).
3. Screw the Backpack onto the base standoffs. Its connectors face the back.
4. Wire the Waveshare terminal to H8–H11 and connect the USB-C cable and the rocker.
5. Drop the Waveshare face-down into the shell. The glass sits in the locating rim behind the window.
6. Fit the 4 clamps. Their pads press the Waveshare PCB at its 4 mounting holes.
7. Lower the base into the shell and fit the 6 countersunk screws from underneath.

## Things to check on the first print
- **Clamp pad length (4.0 mm)** assumes the Waveshare PCB back face sits 6.2 mm behind the glass front. That number is estimated from Waveshare's drawing. Measure yours: if the screen rattles, add foam; if the clamps won't seat, sand the pads. You can also change `PCB_BACK` in kasper_case.py.
- **Waveshare PCB position** relative to the glass is estimated from Waveshare's photo-based drawing. Check the clamp pads land on the mounting holes.
- **DB9 cut-out** is a rounded 20.5 × 11.8 mm slot that works in either D orientation. The hex jack posts pass through Ø6 holes.
- The Backpack's connector edge sits 0.3 mm behind the inside of the back wall. The DC jack face ends about 0.7 mm proud of the wall.

## Placing the Backpack STEP in the assembly
Kasper_Case_Assembly.step leaves the Backpack out to keep the file small. To add it, import `../carrier/REV-9/PCB/3D_PCB1_2026-11-01.step` (REV-10 has the same outline), rotate it 180° about Z, then translate it by (65.72, 96.32, 17.0) mm. Case origin: X = centre, Y = front face, Z = bottom of the base (without feet).
