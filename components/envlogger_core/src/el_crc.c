#include "el_crc.h"

/* Nibble table: 64 bytes of flash instead of 1 kB, plenty fast for 128-byte records. */
static const uint32_t T[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ T[crc & 0xF];
        crc = (crc >> 4) ^ T[crc & 0xF];
    }
    return ~crc;
}

uint32_t el_crc32(const void *data, size_t len) { return el_crc32_update(0, data, len); }
