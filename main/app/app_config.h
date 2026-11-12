#pragma once
/* Build-time constants. Pins and addresses: see docs/HARDWARE.md. */

#define FW_VERSION_STR        "0.1.0-dev"
#define BOARD_REV_STR         "Waveshare ESP32-S3-Touch-LCD-4.3B"

/* ---- Waveshare ESP32-S3 GPIO ---- */
#define PIN_I2C_SDA           8
#define PIN_I2C_SCL           9
#define PIN_TOUCH_INT         4
#define PIN_RS485_RX          43   /* UART0 */
#define PIN_RS485_TX          44   /* UART0 */
#define PIN_CAN_TX            15
#define PIN_CAN_RX            16
#define PIN_USB_DN            19
#define PIN_USB_DP            20

#define LCD_H_RES             800
#define LCD_V_RES             480
#define LCD_PCLK_HZ           (16 * 1000 * 1000)   /* VERIFY vs Waveshare demo (16-21 MHz) */
#define PIN_LCD_VSYNC         3
#define PIN_LCD_HSYNC         46
#define PIN_LCD_DE            5
#define PIN_LCD_PCLK          7
/* RGB565 data order D0..D15 = B3..B7, G2..G7, R3..R7 */
#define LCD_DATA_PINS { 14, 38, 18, 17, 10,  39, 0, 45, 48, 47, 21,  1, 2, 42, 41, 40 }

/* ---- I2C ---- */
#define I2C_FREQ_HZ           400000
#define I2C_ADDR_PCF85063     0x51
#define I2C_ADDR_GT911        0x5D
/* The SHT sensor's address (0x44 / 0x45) and family are settings (sensor_addr, sensor_type). */

/* ---- CH422G bit assignments ---- */
#define EXIO_DI0              0
#define EXIO_TP_RST           1
#define EXIO_BACKLIGHT        2
#define EXIO_LCD_RST          3    /* VERIFY */
#define EXIO_SD_CS            4
#define EXIO_DI1              5
/* No USB_SEL on the 4.3B: USB-C goes straight to GPIO19/20. EXIO5 is DI1 - never drive it. */
#define OD_DO0                0    /* spare open-collector output, kept OFF */
#define OD_DO1                1    /* alarm output */

/* ---- Storage ---- */
#define STORAGE_MOUNT         "/data"
#define STORAGE_PARTITION     "storage"
