# Hardware files

Design files for the **Kasper Backpack**, a battery, USB-C and RS-485 carrier board that sits
behind the Waveshare ESP32-S3-Touch-LCD-4.3B, plus a 3D-printable desktop case and a bench
fixture. The full design description, DNP table, assembly notes and build lessons are in
[OSHWLab_Kasper_Backpack.md](OSHWLab_Kasper_Backpack.md).

> **Before wiring:** do **not** connect the Waveshare terminal's CAN L / CAN H pins (terminal 7/8)
> to the Backpack. They couple noise onto the shared I2C bus and make the screen blink.
> Set the Waveshare VOUT jumper to **3.3 V** and leave its SW2 (bus termination) **off**.

## Folders
| Folder | What | Status |
|---|---|---|
| `carrier/REV-8/` | Carrier board, export 2026-10-30 | **Built (2 boards) and tested** |
| `carrier/REV-9/` | Carrier board, export 2026-11-01 | Superseded by REV-10, not built |
| `carrier/REV-10/` | Carrier board, export 2026-11-23 | **Latest design, not built yet.** Same as REV-9 except U8 = TUSB320LAIRWBR (LCSC C132554, I2C 0x47) |
| `case/` | 3D-printable desktop case (wedge, screen tilted 30°), rev A, sized for REV-9/REV-10 | Design; print settings and assembly in `case/README_CASE.md` |
| `fixtures/` | Waveshare 4.3" development fixture (bench holder) | Design files |

3D-printable versions are also on Cults3D:
- Enclosure and daughterboard mounting: https://cults3d.com/en/3d-model/tool/waveshare-4-3-enclosure-and-daughterboard-mounting
- Stand / bench holder: https://cults3d.com/en/3d-model/tool/waveshare-4-3-lcd-screen-holder-developement

Board outline and mounting holes: [docs/Kasper_Backpack_REV8_dimensions.pdf](../docs/Kasper_Backpack_REV8_dimensions.pdf).

## Carrier board files (EasyEDA Pro exports)
| File | Use |
|---|---|
| `*.epro2` | EasyEDA Pro project. `ProPrj_...epro2` = whole project (schematic + PCB) |
| `PCB/Gerber_*.zip` | Fabrication (send this to the board house) |
| `PCB/BOM_*.xlsx`, `PCB/PickAndPlace_*.xlsx` | Assembly (JLCPCB format, LCSC part numbers) |
| `PCB/PCB_*.pdf` | Board drawing, all layers |
| `PCB/3D_*.step` | 3D model of the assembled board |
| `PCB/Netlist_*`, `PCB/IPC356_*` | Netlists. `FlyingProbeTesting.json` inside the Gerber zip is the authoritative pad-to-net map |
| `PCB/DXF_*`, `Altium_*`, `PADS_*`, `ODB_*`, `Autorouter_*` | Exports for other tools |
| `SCHEMATIC/SCH_*.pdf/.png/.svg` | Schematic. REV-8: the H10/H11 header swap was made after the schematic export; trust the PCB netlist where they differ |

## Notes from the REV-8 build
- **U8:** both built REV-8 boards have a TUSB320L (I2C 0x47), not the TUSB320IRWBR (0x60) in the
  REV-8/REV-9 BOM. REV-10's BOM lists the TUSB320LAIRWBR. The firmware accepts either.
- **Unpopulated parts** are set to "Add into BOM = no" in EasyEDA but have no DNP mark on the
  schematic: R4, R5, R7, R20, R24, R25, R27, R28, R30, H1, H4, H5 (and H7, H12, H13 on REV-8).
  See the DNP table in the OSHWLab description before fitting any of them.
- **DB1 (DB9)** is not in the REV-9/REV-10 BOM: order it separately and hand-solder it.
- **Light pipes** LP1-LP3 (Bivar SLP3-150-100-F) are not in the BOM: order them separately.
- **Standby:** about 175 µA with the rocker off, once the unit has been switched on and off after
  the battery was connected; about 800 µA after a battery reconnect until then.
- **Screen on:** about 570 mA from the cell.

## Sizes
About 250 MB in total. The largest files are the board STEP models (~45 MB each) and two
SolidWorks assemblies (~25 MB each); all are under GitHub's 100 MB per-file limit.
