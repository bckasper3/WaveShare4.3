#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "el_selftest.h"
#include "el_util.h"

const char *el_test_outcome_name(el_test_outcome_t o)
{
    static const char *const N[EL_TEST_OUTCOME_COUNT] = { "PASS", "WARN", "FAIL", "SKIP" };
    return (unsigned)o < EL_TEST_OUTCOME_COUNT ? N[o] : "FAIL";
}

static int find(const el_test_def_t *t, size_t n, const char *id)
{
    for (size_t i = 0; i < n; i++) if (!strcmp(t[i].id, id)) return (int)i;
    return -1;
}

el_err_t el_selftest_validate(const el_test_def_t *t, size_t n)
{
    if (!t || n > EL_SELFTEST_MAX) return EL_ERR_ARG;
    for (size_t i = 0; i < n; i++) {
        if (!t[i].id || !t[i].id[0] || !t[i].run || strchr(t[i].id, ' ')) return EL_ERR_ARG;
        if (find(t, i, t[i].id) >= 0) return EL_ERR_ARG;                 /* duplicate */
        if (t[i].depends_on && find(t, i, t[i].depends_on) < 0) return EL_ERR_ARG;  /* unknown or later */
    }
    return EL_OK;
}

void el_test_set(el_test_report_t *rep, el_test_outcome_t o, float value, const char *fmt, ...)
{
    rep->outcome = o;
    rep->value = value;
    va_list ap; va_start(ap, fmt);
    vsnprintf(rep->detail, sizeof rep->detail, fmt, ap);
    va_end(ap);
}

/* Keep one test's output on one line, whatever the test function wrote. */
static void sanitize(char *s)
{
    for (; *s; s++) if (*s == '\n' || *s == '\r' || *s == '\t') *s = ' ';
}

el_err_t el_selftest_run(const el_test_def_t *t, size_t n, uint32_t mask, void *ctx,
                         el_test_sink_fn sink, void *user, el_test_summary_t *sum)
{
    el_err_t e = el_selftest_validate(t, n);
    if (e != EL_OK) return e;
    /* Outcome per table slot; EL_TEST_OUTCOME_COUNT = not run. */
    uint8_t done[EL_SELFTEST_MAX];
    memset(done, EL_TEST_OUTCOME_COUNT, sizeof done);
    el_test_summary_t s;
    memset(&s, 0, sizeof s);

    for (size_t i = 0; i < n; i++) {
        if (!(t[i].groups & mask)) continue;
        el_test_report_t r;
        int dep = t[i].depends_on ? find(t, n, t[i].depends_on) : -1;
        if (dep >= 0 && (done[dep] == EL_TEST_FAIL || done[dep] == EL_TEST_SKIP)) {
            el_test_set(&r, EL_TEST_SKIP, NAN, "needs %s", t[i].depends_on);
        } else {
            el_test_set(&r, EL_TEST_FAIL, NAN, "test did not report");    /* defensive default */
            t[i].run(ctx, &r);
            if ((unsigned)r.outcome >= EL_TEST_OUTCOME_COUNT) r.outcome = EL_TEST_FAIL;
            r.detail[sizeof r.detail - 1] = '\0';
            sanitize(r.detail);
        }
        done[i] = (uint8_t)r.outcome;
        s.count[r.outcome]++;
        if (sink) sink(user, &t[i], &r);
    }
    if (sum) *sum = s;
    return EL_OK;
}

el_test_outcome_t el_test_judge(float v, const el_test_limits_t *l)
{
    if (!isfinite(v)) return EL_TEST_FAIL;
    if ((!isnan(l->fail_lo) && v < l->fail_lo) || (!isnan(l->fail_hi) && v > l->fail_hi)) return EL_TEST_FAIL;
    if ((!isnan(l->warn_lo) && v < l->warn_lo) || (!isnan(l->warn_hi) && v > l->warn_hi)) return EL_TEST_WARN;
    return EL_TEST_PASS;
}

el_err_t el_selftest_format(char *dst, size_t cap, const el_test_def_t *t, const el_test_report_t *r)
{
    if (!dst || !cap || !t || !r) return EL_ERR_ARG;
    char val[24] = "-";
    if (isfinite(r->value)) snprintf(val, sizeof val, "%.4g", (double)r->value);
    int n = snprintf(dst, cap, "TEST %s %s %s %s %s", t->id, el_test_outcome_name(r->outcome), val,
                     (t->unit && t->unit[0]) ? t->unit : "-", r->detail);
    return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}

el_err_t el_selftest_format_summary(char *dst, size_t cap, const el_test_summary_t *s)
{
    if (!dst || !cap || !s) return EL_ERR_ARG;
    int n = snprintf(dst, cap, "SELFTEST END pass=%u warn=%u fail=%u skip=%u",
                     s->count[EL_TEST_PASS], s->count[EL_TEST_WARN], s->count[EL_TEST_FAIL], s->count[EL_TEST_SKIP]);
    return (n < 0 || (size_t)n >= cap) ? EL_ERR_SIZE : EL_OK;
}
