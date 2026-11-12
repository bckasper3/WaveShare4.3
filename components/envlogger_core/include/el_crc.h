#pragma once
/* CRC-32 (IEEE 802.3 / zlib: poly 0xEDB88320 reflected, init and xorout 0xFFFFFFFF).
 * Check value: el_crc32("123456789", 9) == 0xCBF43926. Same result on PC and ESP32, so files
 * written by the device can be verified by PC tools with any standard CRC-32. */
#include <stddef.h>
#include <stdint.h>

uint32_t el_crc32(const void *data, size_t len);

/* Incremental form: crc = el_crc32_update(0, a, n); crc = el_crc32_update(crc, b, m); */
uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len);
