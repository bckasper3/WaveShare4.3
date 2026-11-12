#include "el_board.h"

/* docs/HARDWARE.md section 2: the Waveshare board's own devices plus the external SHT sensor.
 * The sensor is optional for the bus check (the logger can run on the simulated sensor); it
 * answers at 0x44 or 0x45 depending on the part / ADDR pin.
 * GT911 answers at 0x5D only if the reset/INT strap timing is right; 0x14 showing up as
 * "unexpected" means the strap sequence in display_init() is wrong. */
const el_i2c_expect_t EL_BOARD_I2C_EXPECT[] = {
    { 0x20, 0x27, "CH422G",   true },   /* VERIFY which addresses ACK on a probe */
    { 0x30, 0x3F, "CH422G-IO", true },
    { 0x51, 0x51, "PCF85063", true },   /* RTC */
    { 0x5D, 0x5D, "GT911",    true },   /* touch */
    { 0x44, 0x45, "SHT sensor", false }, /* SHT4x / SHT3x on the I2C terminal */
};
const size_t EL_BOARD_I2C_EXPECT_N = sizeof EL_BOARD_I2C_EXPECT / sizeof EL_BOARD_I2C_EXPECT[0];

bool el_board_i2c_addr_free(uint8_t a)
{
    if (a < 0x08 || a > 0x77) return false;
    for (size_t i = 0; i < EL_BOARD_I2C_EXPECT_N; i++)
        if (a >= EL_BOARD_I2C_EXPECT[i].first && a <= EL_BOARD_I2C_EXPECT[i].last) return false;
    return true;
}
