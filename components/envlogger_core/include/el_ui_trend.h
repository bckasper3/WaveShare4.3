#pragma once
/* Trend chart helpers: downsampling a result series to the chart's point budget and
 * choosing a readable y axis. Pure C, caller-owned buffers, no allocation. */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int64_t t;      /* Unix s */
    float   v;      /* temperature degC or humidity %RH */
} el_ui_pt_t;

/* Largest-Triangle-Three-Buckets downsampling of a time-ordered series: keeps the first and the
 * last point and, per bucket, the point that best preserves the visual shape (so a single
 * spike survives). n <= m copies the input. m < 3 (and n > m) keeps first/last only
 * (m == 2) or the last point (m == 1). Non-finite values are passed through unchanged but never
 * chosen over finite ones. in and out must not overlap. Returns the number written (<= m). */
size_t el_ui_downsample(const el_ui_pt_t *in, size_t n, el_ui_pt_t *out, size_t m);

/* A y axis covering vmin..vmax (fold any limit lines in first) with ~5 % headroom on both
 * sides: lo = a multiple of step, hi = lo + ticks * step, step a "nice" 1, 2, 2.5 or 5 x 10^k.
 * A range narrower than min_span (e.g. 1 degC, so sensor noise does not fill the chart) is
 * widened around its middle. ticks 2..10 (else 5). Non-finite input -> about 0..10.
 * Any output pointer may be NULL. */
void el_ui_nice_range(float vmin, float vmax, uint8_t ticks, float min_span,
                      float *lo, float *hi, float *step);
