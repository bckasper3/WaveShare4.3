# Hardware

The logger runs on one off-the-shelf board, the **Waveshare ESP32-S3-Touch-LCD-4.3B**
(https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B), plus a Sensirion temperature /
humidity sensor on the board's I2C terminal. No other electronics are needed.

Items marked VERIFY are taken from the Waveshare schematic and demo code but have not been
measured on real hardware yet.

## 1. Board summary
| Item | Part / value |
|---|---|
| Module | ESP32-S3-WROOM-1-N16R8: 16 MB flash, 8 MB octal PSRAM |
| Display | 4.3" 800 x 480 RGB565 panel (ST7262), 16-bit parallel RGB |
| Touch | GT911 capacitive, I2C 0x5D |
| IO expander | CH422G (address-as-command, 0x20-0x27 / 0x30-0x3F) |
| RTC | PCF85063A at 0x51, CR1220 backup cell |
| RS-485 | SP3485 on UART0 (RX GPIO43, TX GPIO44), automatic direction |
| Isolated I/O | 2 opto inputs DI0/DI1 (5-36 V), 2 open-collector outputs DO0/DO1 |
| microSD | SPI (MOSI 11, SCK 12, MISO 13), chip select on CH422G EXIO4 |
| USB-C | native ESP32-S3 USB (GPIO19/20): flashing and the CDC serial console |
| Power | 7-36 V on the terminal block, or 5 V on USB-C |

## 2. CH422G IO expander
### 2.1 Protocol
Address-as-command, one data byte per command: `0x24` mode, `0x23` open-collector outputs,
`0x38` IO outputs, `0x26` IO read. Espressif's component skips zero-length writes, so the
firmware has its own small driver (`main/board/ch422g.c`, logic in `el_hw_ch422g`).

### 2.2 Mode
Only two mode values are written: `0x00` (IO0-7 inputs) and `0x01` (IO0-7 outputs). All eight
IO pins switch direction together.

### 2.3 Inputs share the port
DI0 (EXIO0) and DI1 (EXIO5) are opto-transistor collectors with 4.7 k pull-ups. The output
shadow always keeps those two bits 0: driving 1 into an active opto would fight it.

### 2.4 Reading DI0 / DI1
Switch the port to input (mode `0x00`), wait at least 2 ms, read `0x26`, switch back to output
(mode `0x01`). The output latches are kept. While the port is an input the backlight line floats
and is pulled ON and the SD chip select is released, so DI reads and SD transfers take the same
lock (`board_expander_lock`).

### 2.5 Bit map
| EXIO | Function |
|---|---|
| 0 | DI0 (input only) |
| 1 | GT911 reset, active low |
| 2 | Backlight enable / panel DISP, 1 = on |
| 3 | LCD reset (no net on the panel side: harmless) |
| 4 | microSD chip select, active low |
| 5 | DI1 (input only). Some Waveshare demos call this USB_SEL: that is a different board |
| 6, 7 | no net (used by the CH422G self-test read-back) |

## 3. Digital I/O polarity
- Outputs: CH422G OC bit **0 = output transistor ON** (sinking), 1 = off. Power-on default is
  off. DO1 is the alarm output; DO0 is spare and kept off (engineer `IO SET` can drive it).
- Inputs: the raw bit reads **0 when the input is active** (current through the opto LED).
  The firmware inverts it once in `board_read_di()`.
- DI COM floating = dry contact; the terminal's COM is internally fed (VERIFY before wiring a
  powered signal).

## 4. Display
16 MHz pixel clock, HSYNC/HBP/HFP 4/8/8, VSYNC/VBP/VFP 4/8/8, PCLK active negative, two frame
buffers in PSRAM with a bounce buffer, as in Waveshare's `08_lvgl_Porting` demo.

## 5. Touch
GT911 address select: hold TP_RST (EXIO1) and INT (GPIO4) low, release TP_RST while INT is low
-> 0x5D. If 0x14 shows up in the I2C scan, the strap timing was wrong.

## 6. RTC
PCF85063A, CTRL1 = 12.5 pF load capacitance (CAP_SEL), 24 h mode. The RTC keeps UTC; the UI
applies the `utc_offset_min` setting. A clock with the oscillator-stop flag set is reported as
"not set" and the UI opens Set time at boot.

## 7. RS-485
SP3485 on 5 V, direction switched automatically by a transistor on TXD. Logic 1 is not actively
driven (bias resistors hold the bus), so long cables need termination and bias at the master.
UART0 is also the ROM boot console: the firmware disables console output (`CONFIG_ESP_CONSOLE_NONE`,
bootloader log level none), but the ROM still prints a few lines at reset unless the
`UART_PRINT_CONTROL` eFuse is burned (TODO: decide per deployment).

## 8. Backlight
On/off only (CH422G EXIO2 drives the boost converter's enable). Dimming would need a hardware
change, so the "dim" idle stage is equal to "on"; "screen off" works.

## 9. Sensor wiring (I2C terminal)
The terminal block exposes the shared I2C bus (GPIO8 SDA, GPIO9 SCL, 400 kHz) through a level
shifter with 4.7 k pull-ups to I2C_VCC. I2C_VCC is 3.3 V by default (solder pad H8; 5 V option).

| SHT pin | Terminal |
|---|---|
| VDD | 3V3 (I2C_VCC) |
| GND | GND |
| SDA | SDA |
| SCL | SCL |

Supported sensors (setting `sensor_type`, applied at restart):
- **SHT4x** (SHT40 / SHT41 / SHT45), address 0x44 (default) or 0x45 (`-B` variants).
- **SHT3x** (SHT30 / SHT31 / SHT35), address 0x44 (ADDR low) or 0x45 (ADDR high).
Set `sensor_addr` to match. Keep the sensor cable short (< 1-2 m for 400 kHz) and away from
power wiring; a CRC error on a reading is reported as a sensor fault, never as data.
Without a sensor the logger runs on the simulated sensor and flags every result `SIM`.

I2C addresses in use: CH422G 0x20-0x27 / 0x30-0x3F, PCF85063 0x51, GT911 0x5D, SHT 0x44/0x45.

## 10. microSD
SPI, chip select through the CH422G. FAT32 cards up to 32 GB tested by Waveshare. The card is
optional: CSV mirror of every result, event log, export target, firmware update file.

## 11. USB-C
Data lines go straight to GPIO19/20 (no mux, no USB-UART bridge). The firmware runs TinyUSB as
a CDC-ACM device; once it has taken the PHY, flash with BOOT + RESET (or the `BOOTLOADER`
serial command), not with the automatic esptool reset.

## 12. Power
7-36 V on the power terminal or 5 V on USB-C. There is no battery and no power-fail warning:
every persisted write is crash-safe (two-slot NVS blobs, the append-only result log), so the
supply may be cut at any moment.

## 13. GPIO summary
| GPIO | Use |
|---|---|
| 8 / 9 | I2C SDA / SCL (internal bus and terminal) |
| 4 | GT911 INT |
| 43 / 44 | RS-485 RX / TX (UART0) |
| 11 / 12 / 13 | SD MOSI / SCK / MISO |
| 15 / 16 | CAN TX / RX (not used) |
| 19 / 20 | USB D- / D+ |
| 3, 46, 5, 7 | LCD VSYNC, HSYNC, DE, PCLK |
| 14 38 18 17 10 39 0 45 48 47 21 1 2 42 41 40 | LCD data D0-D15 |
