#pragma once
/* Small helpers shared by core modules: bounded string copy and little-endian packing. */
#include <stddef.h>
#include <stdint.h>

/* Copy at most cap-1 chars, always NUL-terminate (cap > 0). Returns strlen(src), like BSD strlcpy,
 * so (ret >= cap) means the copy was truncated. */
size_t el_strlcpy(char *dst, const char *src, size_t cap);

void     el_put_u16le(uint8_t *p, uint16_t v);
void     el_put_u32le(uint8_t *p, uint32_t v);
void     el_put_u64le(uint8_t *p, uint64_t v);
void     el_put_f32le(uint8_t *p, float v);      /* IEEE-754 bits, NAN preserved */
uint16_t el_get_u16le(const uint8_t *p);
uint32_t el_get_u32le(const uint8_t *p);
uint64_t el_get_u64le(const uint8_t *p);
float    el_get_f32le(const uint8_t *p);
