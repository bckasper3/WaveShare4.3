#include <stdio.h>
#include <string.h>
#include "el_app_selftest.h"

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (up(*a) != up(*b)) return false;
    return *a == *b;
}

int el_app_selftest_find(const el_test_def_t *t, size_t n, const char *id)
{
    if (!t || !id) return -1;
    for (size_t i = 0; i < n; i++) if (t[i].id && ieq(t[i].id, id)) return (int)i;
    return -1;
}

el_err_t el_app_selftest_merge(const el_app_test_table_t *tables, size_t ntables,
                               el_test_def_t *out, size_t cap, size_t *n_out,
                               el_app_merge_report_t *rep)
{
    el_app_merge_report_t r;
    memset(&r, 0, sizeof r);
    if (!out || !n_out) return EL_ERR_ARG;
    size_t n = 0;
    for (size_t ti = 0; tables && ti < ntables; ti++) {
        const el_test_def_t *d = tables[ti].defs;
        for (size_t i = 0; d && i < tables[ti].n; i++) {
            if (!d[i].id || !d[i].id[0] || !d[i].run) { r.invalid++; continue; }
            if (el_app_selftest_find(out, n, d[i].id) >= 0) { r.duplicates++; continue; }
            if (n == cap) { r.truncated = true; break; }
            out[n] = d[i];
            /* el_selftest_run rejects a dependency that is unknown or comes later. */
            if (out[n].depends_on && el_app_selftest_find(out, n, out[n].depends_on) < 0) {
                out[n].depends_on = NULL;
                r.deps_cleared++;
            }
            n++;
        }
    }
    *n_out = n;
    if (rep) *rep = r;
    return r.truncated ? EL_ERR_SIZE : EL_OK;
}

el_err_t el_app_selftest_select(const el_test_def_t *t, size_t n, const char *id,
                                el_test_def_t *out, size_t cap, size_t *n_out)
{
    if (!t || !out || !n_out || n > EL_SELFTEST_MAX) return EL_ERR_ARG;
    *n_out = 0;
    int k = el_app_selftest_find(t, n, id);
    if (k < 0) return EL_ERR_ARG;
    bool need[EL_SELFTEST_MAX] = { false };
    /* Follow the chain; at most n hops, so a (malformed) cycle cannot loop forever. */
    for (size_t hop = 0; k >= 0 && hop < n && !need[k]; hop++) {
        need[k] = true;
        k = t[k].depends_on ? el_app_selftest_find(t, n, t[k].depends_on) : -1;
    }
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        if (!need[i]) continue;
        if (m == cap) return EL_ERR_SIZE;
        out[m++] = t[i];
    }
    *n_out = m;
    return EL_OK;
}

uint32_t el_app_selftest_group_mask(const char *w)
{
    if (!w) return 0;
    if (ieq(w, "BOOT"))        return EL_TG_BOOT;
    if (ieq(w, "FULL"))        return EL_TG_BOOT | EL_TG_FULL;
    if (ieq(w, "FIXTURE"))     return EL_TG_FIXTURE;
    if (ieq(w, "INTERACTIVE")) return EL_TG_INTERACTIVE;
    return 0;
}

el_err_t el_app_selftest_format_def(char *dst, size_t cap, const el_test_def_t *t)
{
    if (!dst || !cap || !t) return EL_ERR_ARG;
    static const struct { uint32_t bit; const char *name; } G[] = {
        { EL_TG_BOOT, "BOOT" }, { EL_TG_FULL, "FULL" },
        { EL_TG_FIXTURE, "FIXTURE" }, { EL_TG_INTERACTIVE, "INTERACTIVE" },
    };
    char groups[40] = "";
    size_t gl = 0;
    for (size_t i = 0; i < sizeof G / sizeof G[0]; i++) {
        if (!(t->groups & G[i].bit)) continue;
        int w = snprintf(groups + gl, sizeof groups - gl, "%s%s", gl ? "|" : "", G[i].name);
        if (w > 0) gl += (size_t)w;
    }
    int w = snprintf(dst, cap, "TESTDEF %s %s %s %s", t->id ? t->id : "?", gl ? groups : "-",
                     t->depends_on ? t->depends_on : "-", t->name ? t->name : "");
    return (w < 0 || (size_t)w >= cap) ? EL_ERR_SIZE : EL_OK;
}
