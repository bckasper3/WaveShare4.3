#pragma once
/* Sensor service: a Sensirion SHT4x / SHT3x on the board's I2C terminal (el_sensor_sht), family
 * and address from the settings (sensor_type, sensor_addr; applied at boot). When no sensor
 * answers, the simulated sensor (el_sensor_sim) takes over; every result it produces carries
 * EL_RES_FLAG_SIMULATED ("SIM" in CSV), so simulated data can never pass for real data. */
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "el_sensor.h"
#include "el_selftest.h"

esp_err_t   sensor_init(void);
el_sensor_t sensor_driver(void);        /* handle for el_meas (controller task only) */
bool        sensor_present(void);       /* a real sensor answered at boot */
bool        sensor_simulated(void);
/* on = simulated sensor. off -> ESP_ERR_NOT_SUPPORTED when no real sensor answered. The
 * controller picks the change up when no run is active. */
esp_err_t   sensor_set_simulation(bool on);
esp_err_t   sensor_get_info(char *buf, size_t len);

/* SENSOR (BOOT): one blocking reading with CRC check. */
const el_test_def_t *sensor_selftests(size_t *count);
