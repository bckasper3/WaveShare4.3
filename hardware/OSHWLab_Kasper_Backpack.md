# Kasper Backpack: Battery, USB-C and RS-485 Carrier for the Waveshare ESP32-S3 4.3" Touch LCD

## Video link

**https://youtu.be/OviTJG7jLCk**

---

## Project introduction

The Waveshare ESP32-S3-Touch-LCD-4.3B is a great display module. It has an 800×480 capacitive touch screen, an ESP32-S3 (16 MB flash, 8 MB PSRAM), an RTC, RS-485, CAN, and optocoupled digital I/O. On its own, though, it has no practical way to run untethered. It can't charge a proper battery, it can't host a USB flash drive, and all of its I/O comes out on a 16-pin screw terminal that nobody wants to wire by hand in the field.

**Kasper Backpack** is a 4-layer, 131 × 52 mm carrier board that mounts behind the 4.3B and turns it into a self-contained, portable, rechargeable instrument. It adds:

- A single-cell Li-ion battery, zip-tied to the back of the board, with charging and load-sharing, so the unit runs while it charges.
- Three power inputs: a 12 V barrel jack, 12 V on the DB9, or USB-C.
- A "smart" front USB-C port. It acts as a USB device (serial port) when plugged into a PC and as a USB host that powers a flash drive for data export and firmware updates.
- Battery fuel gauging (voltage, current, coulomb counting) with an INA219.
- RS-485, two digital inputs and one digital output on a single DB9, wired straight to the Waveshare's own transceiver and I/O.
- A panel rocker switch that really disconnects the load. It doesn't just put the board to sleep.
- An internal sensor header (I2C, sensor power, and UPDI for programming a sensor microcontroller).

All SMT parts are on one side, so the board can be reflowed on a hobby hot plate. Every user-facing connector is on the front edge and every Waveshare connection is on the rear edge, which keeps the enclosure design simple.

It started as the electronics for a handheld industrial instrument, but nothing on the board is tied to one application. Use it for any portable data logger, HMI, Modbus terminal or bench gadget built around the 4.3B.

---

## Status: what has been built and tested (November 2026)

Two REV-8 boards have been built and run with a 4.3B for several days, with a phone camera counting screen glitches and the firmware logging every I2C error. Everything we found is written into this page. The board design files are on this OSHWLab project; the firmware is on GitHub (see Software code).

- **Works:** power from battery, USB-C and the load-share switchover; charging; the rocker; USB serial to a PC; the INA219 fuel gauge; the TUSB320 on I2C; the touch screen, RTC and I/O expander sharing the I2C bus; an I2C sensor probe on H1, including plugging it in and out while running.
- **Verification in progress:** flash-drive host mode, RS-485 with a Modbus master, DI/DO loopback, 12 V input on the DB9.
- **Integration notes from the build:**
  - **Do not wire the CAN pins** (see Announcement 1). This was the cause of random screen blinks.
  - **Board 2's TUSB320 (U8) shifted during reflow** and held the whole I2C bus low until it was reflowed (Announcement 6).
  - **The ESP32's USB D+ pull-up leaked into VBUS** and lit LED1 with nothing plugged in. Fixed in firmware by enabling the pull-up only while the TUSB320 reports a cable.
- **Standby measured:** 175 µA with the rocker off, once the unit has been switched on and off after the battery was connected (Announcement 8).
- **Assembly and firmware checks:** after connecting the battery, switch the unit on and off once so the firmware puts the INA219 into power-down (Announcement 8), and check LED4's orientation against the silkscreen (Announcement 10).

A REV-9 layout also exists but has not been built.

---

## Project function

| Function | How it's implemented |
|---|---|
| Run from a battery | 1S Li-ion cell → load-share P-FET (Q4) → rocker P-FET (Q5) → MT3608 boost (U9) → 5.1 V |
| Power the Waveshare | 5.1 V and USB D+/D− go to the Waveshare's **USB-C** port through the rear USB-C (USBC2). Rp pull-ups R22/R23 make the carrier look like a 5 V source |
| Charge the battery | TP4056 (U3), 600 mA, from the merged 5 V input |
| Run while charging | Load sharing: external 5 V feeds SYS through Schottky D5 and turns Q4 off, so the cell only sees charge current |
| 12 V input | Barrel jack (F1 → D1) or DB9 pin 9 (F2 → D2), diode-OR'd, TVS-clamped (D3), AP63205 buck (U1) → 5V_DC |
| USB-C power input | Front USB-C VBUS → P-FET sink switch Q6 → D8 → 5V_IN |
| USB-C device mode | PC attached: the ESP32-S3's native USB enumerates as a CDC serial port (live data stream, commands, bootloader access) |
| USB-C host mode | Flash drive attached: the TUSB320 (U8) pulls USB_ID low. In hardware, that enables the TPS2041B (U2) 5 V source and turns off the sink switch. Firmware switches the USB stack to host/MSC |
| Fuel gauge | INA219 (U5) across the 50 mΩ high-side shunt R17 |
| Charge control | Waveshare output **DO0** drives CHG_EN, so firmware can stop charging (e.g. on a weak USB source) |
| RS-485 | Waveshare's auto-direction transceiver → DB9 pins 1/2. 120 Ω termination is DNP |
| Digital I/O | Waveshare DI0/DI1/DI-COM/DO1 → DB9 pins 6/7/8/4 (not isolated on the Backpack: see section 9) |
| Power switch | Rocker on a JST lead (U7) switches high-side P-FET Q5, which turns the display, the ESP32 and the sensor on and off. It sits after the charger, so **the battery still charges with the rocker off** |
| Status LEDs | External power present (LED1), power on (LED3), charging/charged (LED4/LED2), behind three front light pipes |
| Expansion | Sensor header H1, spare breakout H3 (rails, I2C), test headers H4/H5 |

---

## Project Parameters

| Parameter | Value |
|---|---|
| Board size | 131.44 × 52.32 mm, 1.6 mm, 4 layers |
| Mounting | 4 × Ø4.4 mm holes, 123.83 × 44.20 mm pattern |
| Assembly | All SMT parts on the top side (102 placements) |
| Battery | 1S1P Li-ion/LiPo, 3.7 V nominal, **must have a built-in protection circuit (PCM)**, JST-PH 2.0 lead |
| Recommended cell | 2000–3000 mAh flat pouch (103450 / 104050 class) |
| Charge current | 600 mA (TP4056, set by R13) |
| Charge time | about 4 h for a 2000 mAh cell from empty |
| System rail | 5.1 V (MT3608 boost) |
| Load | ≈550 mA at 5 V with the screen on (measured 564 mA from the cell). Boost input peaks at ≈1.4–1.8 A from a 3.4 V cell |
| 12 V input | 12 V nominal, barrel jack (5.5 × 2.1 mm) or DB9 pin 9, fused, reverse-blocked, TVS-clamped |
| USB-C host output | 5 V, current-limited by the TPS2041B (≈0.5 A continuous) |
| Runtime (2000 mAh) | ≈2.5–3 h screen on (estimate). Longer with backlight dimming and idle timeout |
| Standby (rocker off) | **≈175 µA** (measured) once the unit has been switched on and off. If the battery is unplugged and reconnected, ≈800 µA until the unit is switched on and off again (the INA219 restarts in its power-on mode) |
| RS-485 | Half-duplex, auto-direction (on the Waveshare), termination DNP |
| Digital I/O | 2 × DI, 1 × DO (open drain, 5–36 V, 450 mA), Waveshare optocoupled I/O with its ground tied to board GND |
| I2C devices on carrier | INA219 0x40; TUSB320 0x60 per the BOM, but **both built boards had a TUSB320L at 0x47** (scan for both) |
| Temperature | 0–45 °C while charging (set by the battery) |

### DB9 (female, DB1) pinout. Not RS-232!

| Pin | Signal |
|---|---|
| 1 | RS-485 A |
| 2 | RS-485 B |
| 3 | not connected |
| 4 | DO1, open-drain output |
| 5 | GND |
| 6 | DI0, digital input |
| 7 | DI1, digital input |
| 8 | DI COM |
| 9 | +12 V **input** (fused, diode-OR'd with the barrel jack) |
| Shell | GND via R31 (0 Ω) |

### Waveshare 16-pin terminal → carrier headers (H8–H11, wire pin N to pin N)

| WS pin | Signal | Carrier |
|---|---|---|
| 1 | VIN | H8-1, **not connected**. The Waveshare is powered through USB-C only |
| 2, 4 | GND | H8-2, H8-4 |
| 3 | VOUT (I2C VCC) | H8-3, WS_VOUT. **Must be 3.3 V** (check the Waveshare VOUT setting; measure H8-3) |
| 5 / 6 | SDA / SCL | H9-1 / H9-2. Keep these wires short |
| 7 / 8 | CAN L / CAN H | **Leave unwired.** H9-3 / H9-4 go to spare header H3; wiring them caused screen blinks (Announcement 1) |
| 9 / 10 | RS-485 B / A | H10-1 / H10-2 → DB9 2 / 1 |
| 11 | DO0 | H10-3 → **CHG_EN** (charger enable) |
| 12 | DO1 | H10-4 → DB9 4 |
| 13 | DI COM | H11-1 → DB9 8 |
| 14 | GND | H11-2 |
| 15 / 16 | DI0 / DI1 | H11-3 / H11-4 → DB9 6 / 7 |

---

## Principle analysis

### 1. Power architecture

The board has one job above all others: give the Waveshare a clean 5 V whichever sources are present, and switch between them without a glitch.

```
12V barrel ─F1─D1─┐
DB9 pin 9 ──F2─D2─┴─ 12V_IN ─ TVS(D3) ─ AP63205 buck(U1) ─ 5V_DC ─ D4 ─┐
                                                                       ├─ 5V_IN ─┬─ TP4056(U3) ─ BAT_NODE ─ R17 ─ cell
USB-C VBUS ─ Q6 sink switch ─ VBUS_SINK ─ D8 ──────────────────────────┘        │
                                                                                └─ D5 ─ SYS ◄── Q4 (cell, when no 5V_IN)
SYS ─ Q5 (rocker) ─ SYS_SW ─ MT3608 boost(U9) ─ 5V_OUT 5.1 V ─┬─ rear USB-C → Waveshare (power + D+/D−)
                                                              ├─ TPS2041B(U2) → front USB-C VBUS (host mode only)
                                                              └─ R26 ─ ferrite L2 ─ SENS_PWR → sensor header H1
SYS ─ LDO(U6) ─ 3V3_AON (INA219, TUSB320; always on, even with the rocker off)
```

**Why boost everything from SYS?** SYS swings from about 3.0 V (flat cell) to about 4.7 V (external power through D5). A single boost converter that always regulates to 5.1 V gives the Waveshare the same rail in every mode. When a charger is plugged in or pulled out, only the boost's input changes, never its output.

**Why 5.1 V?** The Waveshare is fed through its USB-C input, which has its own protection and regulator drops behind it. The extra 100 mV covers the connector, cable and diode drop at full backlight load.

**Why power the Waveshare through USB-C and not its VIN terminal?** VIN on the Waveshare (7–36 V) goes through its own buck. Feeding 5 V into USB-C skips that conversion stage, and the same connector carries the ESP32-S3's native USB D+/D− back to the front port. H8-1 (VIN) is deliberately left unconnected, and the Waveshare's own battery switch must stay OFF.

### 2. 12 V entry

The barrel jack and DB9 pin 9 each have their own resettable fuse (F1, F2) and Schottky diode (D1, D2). They are OR'd into `12V_IN`, which a TVS (D3) clamps. Reverse polarity on either input simply blocks. An AP63205 synchronous buck (U1, L1) makes `5V_DC`, which joins USB VBUS on `5V_IN` through D4.

`5V_IN` is an inner-layer copper plane. In earlier revisions, trace resistance between the inputs and the TP4056 ate the charger's dropout headroom and it fell out of constant-current mode. The plane fixed that.

### 3. USB-C sink path

USB VBUS doesn't connect to `5V_IN` directly. It goes through a P-FET (Q6) whose gate (`SINK_G`) is pulled to GND by R9, so the sink path is normally on. In host mode, the same signal that turns on the 5 V source also turns Q6 off (see section 6). That stops the board's own 5 V output from looping back into its charger input.

Measured on REV-8 from a PC port: 4.71 V at the connector but only 4.26 V on `5V_IN` after Q6 and D8. With a nearly full cell the TP4056 is then close to dropout, so the last part of the charge is slow. A lower-drop path than D8 (an ideal diode) is on the list for the next revision.

### 4. Charging and load sharing

The TP4056 is a linear CC/CV charger: 600 mA (R13 sets PROG) up to 4.20 V, then taper and terminate. On its own, a TP4056 has a classic flaw: if the system load hangs on the cell while charging, the load current looks like charge current. Termination never happens and the cell sits at 4.2 V indefinitely.

The fix is a **load-share path**:
- When `5V_IN` is present, it feeds SYS through Schottky D5. It also holds the gate of P-FET **Q4** high, so the cell is disconnected from SYS and sees only the TP4056.
- When external power disappears, R18 pulls `5V_IN` (Q4's gate) to GND. Q4 turns on and the cell takes over SYS. Q4's body diode carries the load during the switchover, so SYS never collapses.

`CHG_EN` (the TP4056 CE pin) is pulled up to `5V_IN` by R14, so charging is enabled by default. It is also wired to the Waveshare's output **DO0**. When firmware turns DO0 on, it sinks CE and charging stops. Firmware can use this when the TUSB320 reports a weak USB source (default 500 mA), or for temperature or storage-charge limits.

The TP4056's CHRG and STDBY outputs only drive LEDs. Firmware infers charge state from the INA219 (current sign and magnitude, ~4.2 V with tapering current = full).

With no cell connected, the TP4056 cycles its output up to 4.2 V and back (LED2 flickers), which an INA219 reading alone mistakes for a full battery. Firmware that shows a battery level should treat "no INA219 reading" as unknown (`--`) rather than reuse the last saved level.

### 5. Power switch

The rocker never carries load current. It sits on a 2-pin JST lead (U7) and pulls the gate of high-side P-FET **Q5** (`PWR_SW_G`) to GND. R19 holds the gate at SYS, so the switch defaults to off. That means:

- Any small panel rocker works.
- With the rocker off, the boost, the Waveshare and the sensor are completely unpowered.
- **Charging still works with the rocker off.** The charger and load-share sit upstream of Q5, so a unit left plugged in overnight while off still charges.
- `R20` (DNP) shorts the gate to GND for an always-on build with no switch.

What stays alive when off is a small LDO (U6) making `3V3_AON` for the INA219 and TUSB320: about **175 µA** in total (measured) with the INA219 powered down. If the battery is unplugged and reconnected, the INA219 restarts in continuous mode and standby is about 800 µA until the unit is next switched on and off with the rocker (section 7).

**Careful:** U4 (battery) and U7 (rocker) are the same 2-pin JST-PH connector. We plugged the rocker into the battery socket once. Label the leads.

### 6. Smart USB-C (TUSB320 + TPS2041B + discrete logic)

The front USB-C (USBC1) has to be a device port or a host port depending on what is plugged in. The **TUSB320** (U8) watches CC1/CC2 as a **Dual-Role Port**, alternating between presenting Rp (source) and Rd (sink). Its `ID` output (`USB_ID`, pulled up by R2) does the hardware switching, so nothing depends on firmware being fast:

**PC or charger attached (we're the sink):** USB_ID stays high, so:
- Q3 is on and ID_INV is low.
- Q2 is off, R10 pulls Q2_G high, and P-FET Q1 is off.
- R9 holds SINK_G low, so **Q6 is on** and VBUS charges the battery.
- SRC_EN_N (from USB_ID through R6) is high, so **the TPS2041B is off**.
- The ESP32-S3 enumerates as a CDC serial device over the D+/D− passed through to the Waveshare.

**Flash drive attached (we're the source):** the TUSB320 pulls USB_ID low, so:
- SRC_EN_N goes low and **the TPS2041B turns on**, putting current-limited 5 V from `5V_OUT` onto VBUS.
- Q3 turns off, ID_INV goes high, Q2 turns on, and Q2_G goes low.
- Q1 turns on and drives SINK_G to 5V_OUT, so **Q6 turns off** and the board's own VBUS can't loop back into its charger.
- Firmware sees the role change over I2C, switches the USB stack to host, and mounts the drive.

`VBUS_DET` (through R1) lets the TUSB320 see VBUS. Its interrupt line (TUSB_INT, pulled up by R3) and the TPS2041B fault flag (USB_OC_N, R8) are **not routed to the ESP32**, because the Waveshare has no free GPIO. Firmware polls the TUSB320 over I2C, and the TPS2041B protects itself. A shorted flash drive gets current-limited instead of dragging down `5V_OUT` and resetting the screen.

D7 is a USB ESD array on D+/D−/VBUS at the front connector. Its VBUS steering diode lets the ESP32's D+ pull-up (3.3 V) leak into VBUS when nothing is plugged in, which lights LED1 faintly and costs about 1 mA. Firmware fix: turn the USB pull-up on only while the TUSB320 reports a cable. Hardware fix for a later revision: an ESD array without a VBUS rail.

**Variants:** the BOM part is the TUSB320IRWBR (I2C 0x60). Both REV-8 boards were assembled with a **TUSB320L** (0x47), which works but has a slightly different register map. Firmware should look at both addresses. The plain TUSB320 needs a short start-up sequence (write 0x04 to register 0x45, wait 30 ms, write 0x00) before it will present Rp, i.e. before host mode works.

**Fallbacks:** if you can't fit the TUSB320 (it's the one stencil-only part), fit R4/R5 (5.1 kΩ CC pull-downs). The port then works as a plain sink/device port with no host mode. Never fit R4/R5 together with U8. Fitting R7 instead of R6 ties SRC_EN_N high, which permanently disables the 5 V source.

### 7. Battery monitoring (INA219)

The INA219 (U5, 0x40) measures across the 50 mΩ shunt R17, which sits **high-side** between the battery's + terminal (VIN+) and BAT_NODE (VIN−). Positive current means discharging. The bus-voltage register reads BAT_NODE. Use the 40 mV PGA range (±800 mA) for resolution.

The INA219 is on the always-on rail and **powers up in continuous mode** (about 0.7 mA) whenever the battery is connected. Firmware should run it **triggered (single-shot)**: one conversion, then power-down, every few seconds, and put it in power-down at every boot. Anything that leaves it in continuous mode (a cell plugged in while the rocker is off, or firmware that never touches it) costs about 0.7 mA of standby.

State of charge can combine coulomb counting while running with an open-circuit-voltage lookup after rest, and runtime remaining as an average of the discharge current.

### 8. RS-485

The transceiver lives on the Waveshare (UART0, GPIO43/44, auto-direction). The Backpack just routes A/B from H10 to DB9 pins 1/2. Firmware runs it as a Modbus RTU slave.

Auto-direction transceivers have a quirk: they **actively drive only a "0"**. For a "1" they release the bus and rely on bias to hold the idle state. So the bus needs failsafe bias somewhere (typically at the master), and you should be careful about termination.

**Termination (R28 + R30 in series, through TERM_B) is deliberately unpopulated.** A device that gets plugged into unknown buses shouldn't assume it's at the end of the line. An extra 120 Ω in the middle of a bus kills the bias margin, and the auto-direction "1" bits are the first thing to fail. Terminate at the physical ends of the bus only. The Waveshare's own termination switch (SW2) must stay OFF.

Because GPIO43/44 are the ESP32's default console pins, set the firmware console to USB (or none) so log text never goes out on the RS-485 bus.

### 9. Digital I/O

DI0, DI1 and DI-COM on the DB9 go straight to the Waveshare's optocoupled inputs. DO1 goes to its open-drain output (5–36 V, 450 mA). The Waveshare's I/O ground is tied to board GND at H11-2, which **defeats the isolation**. That's intentional, for a simple single-ground handheld. DO0 isn't exposed; it's used internally as the charger enable.

### 10. Sensor power and I2C

`SENS_PWR` feeds only the internal header H1 (and test points). It comes from `5V_OUT` through R26 and a ferrite (L2), so the sensor is powered whenever the rocker is on. For a 12 V sensor, fit R27 instead of R26. **Never fit both.** That would tie the 12 V input to the 5 V rail.

The I2C bus (GPIO8/9) is shared with the Waveshare's own GT911 touch, PCF85063 RTC and CH422G expander. It already has 3.3 V pull-ups on the Waveshare, and the terminal's SDA/SCL go through a MOSFET level shifter with 4.7 kΩ to VOUT. R24/R25 are spare pull-ups to WS_VOUT, DNP. 2.2 kΩ there makes the lines stiffer against noise, but **only fit them with VOUT on 3.3 V**: the TUSB320's I2C pins are rated to 3.6 V.

A sensor that powers up with the rocker may not answer for the first second or two, so firmware should look for it again after boot instead of giving up at the first scan.

### 11. Layout notes

- 4 layers: signal / GND / power planes (5V_IN, 5V_OUT, SYS) / signal.
- The buck and boost switching loops (IC, inductor, diode, input/output caps) are kept tight on the top layer over unbroken GND. The MT3608's inductor (L3) is audible; a fully shielded 10–22 µH part is on the list for the next revision.
- Battery → R17 → Q4 → SYS → Q5 → boost carries up to about 1.8 A from a low cell. It uses pours and stitched vias, not traces.
- Front edge: three light pipes, front USB-C, barrel jack, DB9. Rear edge: H8–H11 to the Waveshare terminal, and the rear USB-C to the Waveshare's USB-C (leave about 10 mm behind it for the plug).

### 12. Noise between the boards (what the testing taught us)

The I2C link between the two boards is the part of this design that needs the most care. It is high-impedance, it runs over the terminal wiring, and the Waveshare's CH422G I/O expander (which drives the backlight and the LCD/touch reset lines) treats **every address from 0x20 to 0x3F as a command**, so noise on SDA/SCL can switch the backlight off or reset the touch chip.

With the full harness wired, the screen blinked off for about half a second every 1–4 minutes, 45–100 I2C transfers a minute failed, and twice the whole bus locked up until reset. We added one wire group at a time, starting from no wires, with a phone camera counting blinks. The cause was the **CAN L/H pair** (terminal pins 7/8):

| Wiring | Blinks | Failed I2C transfers |
|---|---|---|
| No wires | 0 in 37 min | 0 |
| Grounds + VOUT + SDA/SCL | 0 | ≈0 |
| + CAN L/H | 6 in 26 min | ≈100 a minute |
| Everything except CAN, battery connected | 0 in 36 min | 0 |
| Everything, with the Waveshare's 120 Ω CAN termination on | ≈7 in 11 min | ≈36 a minute |
| Everything, sending real CAN frames (1 or 100 a second) | no change | ≈45–49 a minute |

The unused CAN pair is only held by the transceiver's ~15 kΩ bias, so it picks up noise from the boost converter and carries it into the Waveshare right next to SDA/SCL. A 120 Ω terminator only stops differential noise, not noise that rides both wires together. The transceiver (TJA1051, TXD internally pulled up) was not the cause, and neither was the firmware's I2C traffic.

**Rules that came out of it:** don't wire signals you don't use; keep I2C short with a ground beside each line (twist SDA with GND and SCL with GND if the wires are long); route the bundle away from the boost inductor; keep VOUT at 3.3 V. If you need CAN, give it split termination (2 × 60 Ω with 4.7 nF to GND) or a common-mode choke, and keep it away from the I2C wires.

Firmware can soften the effect when noise does get through: rewrite the CH422G's output registers every ~50 ms so a disturbance lasts milliseconds, reset and clock the I2C bus free after a few failed transfers, and read the touch chip on its interrupt pin instead of polling it.

---

## Software code

Firmware, design files and docs: **https://github.com/bckasper3/WaveShare4.3**

The repo's firmware is an **ESP32-S3 Environment Logger**: a touchscreen temperature and humidity logger for the 4.3B with a Sensirion SHT4x/SHT3x sensor. It logs to crash-safe internal flash, raises high/low alarms (with the DO1 output), and exports over USB serial, Modbus RTU on RS-485, CSV on microSD, and an optional send-only Wi-Fi upload. It runs on the 4.3B alone or on the Backpack.

Stack: **ESP-IDF 5.5**, **LVGL 8.3**, **esp_tinyusb** (CDC serial device), and its own Modbus RTU slave engine. Product logic is plain C, unit-tested on a PC, and the whole UI runs in a PC simulator.

The Backpack-specific features are not in the logger firmware yet. To use them, your firmware needs to:
- **Put the INA219 into power-down** at boot and use it single-shot, or standby current rises by ~0.7 mA. Then it can read the fuel gauge.
- **Poll the TUSB320** over I2C at 0x60 or 0x47 for the cable state and host mode, and use DO0 to stop charging on a weak USB source.
- **Leave the CAN TX pin (GPIO15) high** if CAN is not used.
- **Look for the sensor on H1 again after boot** (every couple of seconds), not just once: a sensor powered by the rocker may not answer in the first second or two.

---

## Announcements (pitfalls and lessons learned)

1. **Don't wire the CAN pins to the Backpack.** Leave Waveshare terminal pins 7/8 open unless you really need CAN. The unused CAN pair carried noise onto the I2C bus and made the screen blink (section 12).
2. **Keep the I2C wiring short and grounded.** A ground beside each signal, twisted if long, away from the boost inductor. Keep the Waveshare VOUT at 3.3 V.
3. **RS-485: bias matters more than termination.** The Waveshare's auto-direction transceiver only drives "0". Without failsafe bias the bus floats on "1" bits and you get garbage. Don't terminate "just to be safe": an extra terminator mid-bus kills the bias margin. R28/R30 stay off unless this unit is always a bus end. 560 Ω bias resistors (A to 5 V, B to GND) beside the DB9 are planned for a later revision. On REV-8, make sure your master provides bias.
4. **Power traces are the hidden failure.** Early revisions charged at a fraction of the set current because a few hundred milliohms of thin trace stole the TP4056's headroom. Use planes or pours for anything over about 300 mA, and stitch vias at every layer change (several per amp).
5. **Group components by function, not schematic sheet.** Keep each converter's input cap, IC, inductor, diode and output caps together and tight. Grouping also makes bring-up easier: each block can be probed on its own at H4/H5.
6. **Inspect the TUSB320 after reflow.** On one board U8 shifted and held the whole I2C bus low from power-up (no touch, no clock). A reflow fixed it. Use the stencil; TI's recommended stencil is only 0.1 mm thick, and too much paste bridges under the part.
7. **Scan for the TUSB320 at both 0x60 and 0x47.** The BOM part and the TUSB320L that was actually assembled use different addresses.
8. **After the battery is unplugged and reconnected, switch the unit on and off once.** The INA219 restarts in continuous mode (about 0.7 mA) whenever its supply is interrupted, so standby is about 800 µA until the firmware powers it down at the next boot. After that it is about 175 µA. When measuring standby current, keep the battery circuit unbroken: changing meter jacks or ranges disconnects the battery and resets the INA219. Put a jumper across the meter while switching the rocker or the meter, then remove it to read.
9. **Battery and rocker use the same JST-PH connector** (U4 and U7). Label the leads before you plug anything in.
10. **Check LED4's orientation** against the cathode mark on the silkscreen during assembly.
11. **Either/or resistors:** R26 or R27 (sensor voltage), R6 or R7 (host mode enable/disable), TUSB320 or R4/R5. Fitting both of a pair shorts rails or breaks USB-C.
12. **Feed the Waveshare through USB-C only.** Leave its VIN terminal unconnected, its battery switch OFF and its RS-485/CAN termination switch (SW2) OFF.
13. **The battery MUST have its own protection circuit.** See below.
14. **JLC "Extended" parts cost about $3 each in loading fees.** The BOM sticks to Basic/Preferred parts wherever possible. Check before swapping a 1-cent resistor for one that costs $3 to load.
15. **Header orientation:** H8–H11 read Waveshare pins 1–16 left to right. We had two of them swapped once.

---

## Assembling processes

### Single-sided, hot-plate friendly

All 102 SMT placements are on the **top side**, and the bottom is flat (it's where the battery goes). So you can:

1. Paste the top side with a stencil or a syringe.
2. Place parts with tweezers. Everything is 0603/0805 passives, SOT-23/SOT-23-5/6, SOP-8 and similar.
3. Reflow the whole board on a **hot plate**. No second side, no glue.
4. Hand-solder the through-hole parts: DB9, barrel jack, vertical JST-PH battery and switch headers (U4, U7), and any optional pin headers.

### Stencil recommended for one part

Everything except one part can be syringe-pasted. **The TUSB320 (X2QFN-12, 1.6 × 1.6 mm, 0.4 mm pitch)** really needs a stencil, because hand-dispensed paste at 0.4 mm pitch almost always bridges. Order the cheap frameless stencil with the PCBs. If you'd rather skip it, leave U8 off and fit R4/R5. You keep USB serial and charging, and lose flash-drive host mode.

Suggested profile: preheat to ~150 °C and soak 60–90 s, then peak ~235–245 °C for lead-free paste (~210 °C for leaded). Inspect the TUSB320, AP63205 and MT3608 under magnification, then wash the flux. A TUSB320 that answers on I2C at its address proves its supply, address, enable, SDA and SCL pads are all soldered.

### First power-up

1. **No battery, no Waveshare.** Apply 12 V to the barrel jack from a bench supply limited to 200 mA. Check `12V_IN`, `5V_DC` and `5V_IN` on H4. LED1 should light.
2. Fit a rocker (or short U7). Check `SYS_SW` on H4 and `5V_OUT` = 5.1 V on H5. LED3 should light.
3. Connect a bench supply set to 3.7 V, 1 A limit, to U4 (pin 1 = +). Check that charge current flows and the charge LED lights.
4. Remove the 12 V. `5V_OUT` should stay up with no dropout, now running from "battery".
5. Plug the front USB-C into a PC and check that it charges. Plug in a flash drive and check for 5 V on VBUS.
6. Connect the Waveshare (USB-C plus the H8–H11 terminal wiring, **without CAN L/H**) and flash the firmware. Run an I2C scan: you should see 0x40 (INA219), 0x60 or 0x47 (TUSB320), 0x51 (RTC), 0x5D (touch) and the CH422G.

### Unpopulated (DNP) positions: what they're for

| Ref | Connection | Purpose | When to fit |
|---|---|---|---|
| **R4, R5** | CC1 / CC2 → GND (5.1 kΩ) | Plain USB-C sink pull-downs | Only if the TUSB320 (U8) is **not** fitted |
| **R7** | 5V_OUT → SRC_EN_N | Holds the TPS2041B off: permanently disables host mode | Instead of R6, for a device-only build. Never both |
| **R20** | PWR_SW_G → GND | Bypasses the rocker: always on | Builds without a panel switch |
| **R24, R25** | SDA / SCL → WS_VOUT | Extra I2C pull-ups (2.2 kΩ) | Long sensor leads, slow edges, or extra noise margin. **Only with VOUT at 3.3 V** |
| **R27** | 12V_IN → SENS_SEL | Powers the sensor from 12 V instead of 5 V | 12 V sensors. **Never with R26** |
| **R28, R30** | RS485_A – TERM_B – RS485_B | 120 Ω termination (two resistors in series) | Only if this unit is always a bus end |
| **H1** | UPDI, SDA, SCL, SENS_PWR, GND (1.27 mm) | Internal sensor daughterboard | When a sensor module is used. Hand-fit |
| **H3** | SYS, 5V_OUT, 3V3_AON, SDA, SCL, CANH, CANL, GND | Spare expansion header | Add-ons. Don't use its CAN pins with the terminal CAN wires connected (Announcement 1) |
| **H4** | 12V_IN, 5V_DC, VBUS_EXT, VBUS_SINK, 5V_IN, BAT_NODE, SYS, SYS_SW | Power test header, every rail in order from input to switched system | Bring-up and debugging |
| **H5** | 5V_OUT, 3V3_AON, SDA, SCL, USB_ID, SENS_PWR, WS_VOUT, CHG_EN | Signal test header (USB_ID shows the USB role, CHG_EN shows charger state) | Bring-up and debugging |
| **H7** | UPDI | Program the sensor's AVR microcontroller in place | Sensor development |
| **H12** | SENS_PWR | Test point | Debugging |
| **H13** | GND | Scope ground | Debugging |

### Battery installation

The battery mounts on the **flat back of the Backpack**, held with **zip ties through the slots in the board**. All components are on the top side, so nothing sits under the cell.

- **Cell:** a standard **1S1P** Li-ion or LiPo pack (one cell, 3.7 V nominal, 4.2 V full) with a **JST-PH 2.0 mm** lead. A 2000–3000 mAh flat pouch (103450 / 104050 size) fits well inside the board outline.
- **Polarity:** U4 pin 1 = **+** (BATT_P), pin 2 = GND. Hobby battery leads are *not* standardized. Check with a meter before plugging in. A reversed cell will destroy the charger. And make sure it's U4, not the rocker's U7.
- **Protection: required.** Use a pack with a **built-in protection circuit (PCM)**. It's the small board under the tape at the end of most hobby LiPo packs. The Backpack's TP4056 only *charges*. There is **no hardware over-discharge, over-current or short-circuit cutoff on the board**. Firmware can warn at a low voltage and ask to switch off, but only while the rocker is on and the firmware is running. It can't save the cell from a fault, a crashed program, or a pinched lead. The PCM should cut off at about 2.5–3.0 V and a few amps.
- **Mounting:** put a foam pad or a few layers of Kapton between the cell and the PCB so through-hole pins and vias can't chafe the pouch. Pull the zip ties snug, not crushing. Pouch cells shouldn't be squeezed, and they swell slightly with age.
- **Runtime:** with a 2000 mAh cell, about **2.5–3 h** continuous with the screen at full brightness (estimate; the screen-on load measured 564 mA from the cell). Expect more with backlight dimming and an idle timeout. A 3000 mAh cell gives roughly 1.5× those figures.
- **Standby:** about **175 µA** with the rocker off (measured; roughly 1.3 years on a 2000 mAh cell), once the unit has been switched on and off. **If the battery is unplugged and reconnected, standby rises to about 800 µA** (about 3–4 months) until the unit is switched on and off again. Don't store it flat: a protected cell that sits at its cutoff for months may refuse to charge.
- **Charging:** about 4 h for 2000 mAh at 600 mA, from USB-C or 12 V, with the rocker on or off.

---

## Finished product display

**Firmware on GitHub:** https://github.com/bckasper3/WaveShare4.3 (ESP-IDF 5.5 firmware, PC unit tests and UI simulator)

**Board design files (EasyEDA Pro project, Gerbers, BOM, pick-and-place, schematic, STEP), REV-8 / REV-9 / REV-10:** https://github.com/bckasper3/WaveShare4.3/tree/main/hardware/carrier

**Case and fixture CAD (STEP, SolidWorks, CadQuery source):** https://github.com/bckasper3/WaveShare4.3/tree/main/hardware/case

**Dimension drawing (PDF):** https://github.com/bckasper3/WaveShare4.3/blob/main/docs/Kasper_Backpack_REV8_dimensions.pdf

**3D-printable stand / holder for the Waveshare 4.3":** https://cults3d.com/en/3d-model/tool/waveshare-4-3-lcd-screen-holder-developement

**3D-printable enclosure and daughterboard mounting:** https://cults3d.com/en/3d-model/tool/waveshare-4-3-enclosure-and-daughterboard-mounting
