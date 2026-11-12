#include <stdio.h>
#include <string.h>
#include "el_ui_status.h"
#include "el_util.h"

/* Light theme for a bright room: saturated mid-tones on white, AA contrast for text.
 * Amber is too light for white text, so its on_fg is near-black. Dark theme: lifted tones,
 * dark text on the strong colours. Accent = teal: white on 0B7577 is 5.5:1, 0B7577 on the page background 5.0:1. */
static const el_ui_tone_colors_t LIGHT[EL_UI_TONE_COUNT] = {
    [EL_UI_TONE_NEUTRAL] = { 0x6B7684, 0xEEF1F4, 0xFFFFFF, 0x4A5562 },
    [EL_UI_TONE_OK]      = { 0x18803A, 0xE6F4EA, 0xFFFFFF, 0x146C2E },
    [EL_UI_TONE_WARN]    = { 0xF2A900, 0xFEF4D9, 0x1A1A1A, 0x8A5A00 },
    [EL_UI_TONE_BAD]     = { 0xD32F2F, 0xFDECEA, 0xFFFFFF, 0xB71C1C },
    [EL_UI_TONE_FAULT]   = { 0x7B1FA2, 0xF3E5F5, 0xFFFFFF, 0x6A1B9A },
    [EL_UI_TONE_ACCENT]  = { 0x0B7577, 0xE0F2F1, 0xFFFFFF, 0x0B6E70 },
};
static const el_ui_tone_colors_t DARK[EL_UI_TONE_COUNT] = {
    [EL_UI_TONE_NEUTRAL] = { 0x8A96A3, 0x232B35, 0x0E1318, 0xB5C0CB },
    [EL_UI_TONE_OK]      = { 0x34A853, 0x15301E, 0x07140B, 0x6DD58C },
    [EL_UI_TONE_WARN]    = { 0xF5B82E, 0x3A2E10, 0x1A1300, 0xF7C858 },
    [EL_UI_TONE_BAD]     = { 0xEF5350, 0x3B1717, 0x1A0505, 0xFF8A80 },
    [EL_UI_TONE_FAULT]   = { 0xB26BD9, 0x2E1A3A, 0x12051A, 0xD1A3EC },
    [EL_UI_TONE_ACCENT]  = { 0x2BB5B0, 0x0D2C2C, 0x031413, 0x7ED8D3 },
};

void el_ui_tone_colors(el_ui_tone_t tone, bool dark, el_ui_tone_colors_t *out)
{
    if (!out) return;
    if ((unsigned)tone >= EL_UI_TONE_COUNT) tone = EL_UI_TONE_NEUTRAL;
    *out = dark ? DARK[tone] : LIGHT[tone];
}

el_ui_tone_t el_ui_status_tone(el_result_status_t s)
{
    switch (s) {
    case EL_RES_NORMAL: return EL_UI_TONE_OK;
    case EL_RES_ALARM:  return EL_UI_TONE_BAD;
    case EL_RES_ERROR:  return EL_UI_TONE_FAULT;
    default:            return EL_UI_TONE_NEUTRAL;
    }
}

el_ui_glyph_t el_ui_status_glyph(el_result_status_t s)
{
    switch (s) {
    case EL_RES_NORMAL: return EL_UI_GLYPH_CHECK;
    case EL_RES_ALARM:  return EL_UI_GLYPH_ALARM;
    case EL_RES_ERROR:  return EL_UI_GLYPH_FAULT;
    default:            return EL_UI_GLYPH_NONE;
    }
}

const char *el_ui_status_label(el_result_status_t s)
{
    switch (s) {
    case EL_RES_NORMAL: return "NORMAL";
    case EL_RES_ALARM:  return "ALARM";
    case EL_RES_ERROR:  return "ERROR";
    default:            return "NO RESULT";
    }
}

const char *el_ui_alarm_text(uint8_t a)
{
    bool t = (a & (EL_ALARM_T_LOW | EL_ALARM_T_HIGH)) != 0, h = (a & (EL_ALARM_RH_LOW | EL_ALARM_RH_HIGH)) != 0;
    if (t && h)                 return "Temp. and RH";
    if (a & EL_ALARM_T_HIGH)    return "Temp. high";
    if (a & EL_ALARM_T_LOW)     return "Temp. low";
    if (a & EL_ALARM_RH_HIGH)   return "Humidity high";
    if (a & EL_ALARM_RH_LOW)    return "Humidity low";
    return "";
}

const char *el_ui_fault_title(el_meas_fault_t f)
{
    switch (f) {
    case EL_MEAS_FAULT_NONE:                return "No fault";
    case EL_MEAS_FAULT_SENSOR_NO_ACK:       return "Sensor not responding";
    case EL_MEAS_FAULT_SENSOR_TIMEOUT:      return "Sensor timed out";
    case EL_MEAS_FAULT_SENSOR_OUT_OF_RANGE: return "Reading out of range";
    case EL_MEAS_FAULT_SENSOR_CRC:          return "Sensor data corrupted";
    case EL_MEAS_FAULT_SENSOR_INTERNAL:     return "Sensor error";
    case EL_MEAS_FAULT_WATCHDOG:            return "Measurement timed out";
    case EL_MEAS_FAULT_BAD_SAMPLE:          return "Invalid reading";
    case EL_MEAS_FAULT_STUCK:               return "Sensor reading stuck";
    case EL_MEAS_FAULT_NO_DRIVER:           return "No sensor driver";
    default:                                return "Measurement failed";
    }
}

const char *el_ui_fault_hint(el_meas_fault_t f)
{
    switch (f) {
    case EL_MEAS_FAULT_SENSOR_NO_ACK:
        return "Check the sensor wiring (SDA, SCL, 3.3 V, GND) and the sensor type and address in Settings.";
    case EL_MEAS_FAULT_SENSOR_TIMEOUT:
    case EL_MEAS_FAULT_WATCHDOG:
        return "Check the sensor cable, then try again.";
    case EL_MEAS_FAULT_SENSOR_CRC:
        return "Noise on the I2C cable. Keep the sensor cable short and away from power wiring.";
    case EL_MEAS_FAULT_SENSOR_OUT_OF_RANGE:
        return "The conditions are outside the sensor's range.";
    case EL_MEAS_FAULT_STUCK:
    case EL_MEAS_FAULT_BAD_SAMPLE:
        return "Restart the logger. If it repeats, run Diagnostics.";
    case EL_MEAS_FAULT_NO_DRIVER:
    case EL_MEAS_FAULT_SENSOR_INTERNAL:
        return "Run Diagnostics. Replace the sensor if it repeats.";
    default:
        return "No result was saved.";
    }
}

const char *el_ui_reject_text(el_meas_reject_t r)
{
    switch (r) {
    case EL_MEAS_ACCEPTED:           return "Measurement started";
    case EL_MEAS_REJ_INVALID:        return "Invalid measurement settings";
    case EL_MEAS_REJ_NOT_ACTIVE:     return "Nothing to cancel";
    case EL_MEAS_REJ_BUSY:           return "A measurement is already running";
    case EL_MEAS_REJ_RESULT_PENDING: return "Previous result is still being saved";
    case EL_MEAS_REJ_EXPORT_BUSY:    return "Export in progress - wait until it finishes";
    case EL_MEAS_REJ_LOG_FULL:       return "Result log full - export and clear it";
    default:                         return "Measurement refused";
    }
}

void el_ui_level_badge(el_level_t level, el_ui_badge_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->show = true;
    switch (level) {
    case EL_LEVEL_ADMIN:    out->tone = EL_UI_TONE_ACCENT; break;
    case EL_LEVEL_ENGINEER: out->tone = EL_UI_TONE_FAULT;  break;
    default:                out->tone = EL_UI_TONE_NEUTRAL; level = EL_LEVEL_OPERATOR; break;
    }
    el_strlcpy(out->text, el_level_name(level), sizeof out->text);
}
