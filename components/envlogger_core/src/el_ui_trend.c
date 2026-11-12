#include <math.h>
#include "el_ui_trend.h"

size_t el_ui_downsample(const el_ui_pt_t *in, size_t n, el_ui_pt_t *out, size_t m)
{
    if (!in || !out || n == 0 || m == 0) return 0;
    if (n <= m) {
        for (size_t i = 0; i < n; i++) out[i] = in[i];
        return n;
    }
    if (m == 1) { out[0] = in[n - 1]; return 1; }
    if (m == 2) { out[0] = in[0]; out[1] = in[n - 1]; return 2; }

    /* Classic LTTB: the inner n-2 points are split into m-2 buckets; from each bucket keep the
     * point forming the largest triangle with the previously kept point and the average of the
     * next bucket. Bucket edges use integer maths so every inner point lands in one bucket. */
    size_t o = 0, a = 0;
    size_t inner = n - 2, buckets = m - 2;
    out[o++] = in[0];
    for (size_t b = 0; b < buckets; b++) {
        size_t lo = 1 + (b * inner) / buckets;
        size_t hi = 1 + ((b + 1) * inner) / buckets;           /* exclusive */
        size_t nlo = hi;
        size_t nhi = (b + 1 < buckets) ? 1 + ((b + 2) * inner) / buckets : n;
        double ax = 0.0, ay = 0.0;
        size_t cnt = 0;
        for (size_t i = nlo; i < nhi; i++) {
            if (!isfinite(in[i].v)) continue;
            ax += (double)in[i].t; ay += (double)in[i].v; cnt++;
        }
        if (cnt) { ax /= (double)cnt; ay /= (double)cnt; }
        else     { ax = (double)in[nhi - 1].t; ay = 0.0; }
        double px = (double)in[a].t, py = isfinite(in[a].v) ? (double)in[a].v : 0.0;
        size_t best = lo;
        double best_area = -1.0;
        for (size_t i = lo; i < hi; i++) {
            if (!isfinite(in[i].v)) continue;
            double area = fabs((px - ax) * ((double)in[i].v - py) -
                               (px - (double)in[i].t) * (ay - py));
            if (area > best_area) { best_area = area; best = i; }
        }
        out[o++] = in[best];
        a = best;
    }
    out[o++] = in[n - 1];
    return o;
}

static float nice_step(float raw)
{
    float mag = powf(10.0f, floorf(log10f(raw)));
    float f = raw / mag;
    float nice = f <= 1.0f ? 1.0f : f <= 2.0f ? 2.0f : f <= 2.5f ? 2.5f : f <= 5.0f ? 5.0f : 10.0f;
    return nice * mag;
}

void el_ui_nice_range(float vmin, float vmax, uint8_t ticks, float min_span,
                      float *lo, float *hi, float *step)
{
    if (ticks < 2 || ticks > 10) ticks = 5;
    if (!isfinite(min_span) || min_span <= 0.0f) min_span = 1.0f;
    if (!isfinite(vmin) || !isfinite(vmax)) { vmin = 0.0f; vmax = 10.0f; }
    if (vmin > vmax) { float t = vmin; vmin = vmax; vmax = t; }
    float span = vmax - vmin;
    if (span < min_span) {                     /* a flat series: centre it in min_span */
        float mid = (vmin + vmax) * 0.5f;
        vmin = mid - min_span * 0.5f;
        vmax = mid + min_span * 0.5f;
        span = min_span;
    }
    /* ~5 % headroom on both sides, then widen the step until the grid covers the range. */
    float pad = span * 0.05f;
    vmin -= pad;
    vmax += pad;
    float s = nice_step((vmax - vmin) / (float)ticks);
    float l = floorf(vmin / s) * s;
    while (l + s * (float)ticks < vmax) {
        s = nice_step(s * 1.01f);
        l = floorf(vmin / s) * s;
    }
    if (lo) *lo = l;
    if (hi) *hi = l + s * (float)ticks;
    if (step) *step = s;
}
