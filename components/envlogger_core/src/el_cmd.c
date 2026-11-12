#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "el_cmd.h"

static char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

static bool ieq_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) if (upper(a[i]) != upper(b[i])) return false;
    return true;
}

static void vemit(el_cmd_ctx_t *ctx, const char *prefix, const char *fmt, va_list ap)
{
    char buf[256];
    int n = snprintf(buf, sizeof buf, "%s", prefix);
    if (n < 0) return;
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    size_t len = m < 0 ? (size_t)n : (size_t)n + (size_t)m;
    if (len > sizeof buf - 2) len = sizeof buf - 2;       /* keep room for "\n" */
    buf[len] = '\n';
    buf[len + 1] = '\0';
    ctx->out(ctx->user, buf);
}

void el_cmd_reply(el_cmd_ctx_t *ctx, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vemit(ctx, "", fmt, ap);
    va_end(ap);
}

el_cmd_err_t el_cmd_error(el_cmd_ctx_t *ctx, el_cmd_err_t code, const char *fmt, ...)
{
    char prefix[16];
    snprintf(prefix, sizeof prefix, "ERR %d ", (int)code);
    va_list ap; va_start(ap, fmt);
    vemit(ctx, prefix, fmt, ap);
    va_end(ap);
    return code;
}

/* Split on spaces/tabs; "double quoted" tokens may contain spaces. */
static el_cmd_err_t tokenize(char *p, char **tok, int max, int *count)
{
    int n = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n == max) return EL_CMD_ERR_SYNTAX;
        if (*p == '"') {
            tok[n++] = ++p;
            while (*p && *p != '"') p++;
            if (!*p) return EL_CMD_ERR_SYNTAX;
            *p++ = '\0';
            if (*p && *p != ' ' && *p != '\t') return EL_CMD_ERR_SYNTAX;
        } else {
            tok[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    *count = n;
    return EL_CMD_OK;
}

/* Number of tokens consumed by the command name, or 0 if it does not match. */
static int match_name(const char *name, char **tok, int ntok)
{
    int w = 0;
    const char *p = name;
    while (*p) {
        if (w >= ntok) return 0;
        const char *s = p;
        while (*p && *p != ' ') p++;
        size_t len = (size_t)(p - s);
        if (strlen(tok[w]) != len || !ieq_n(s, tok[w], len)) return 0;
        w++;
        while (*p == ' ') p++;
    }
    return w;
}

el_cmd_err_t el_cmd_execute(const el_cmd_def_t *table, size_t n, char *line, el_cmd_ctx_t *ctx)
{
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';

    char *tok[EL_CMD_MAX_ARGS];
    int ntok = 0;
    if (tokenize(line, tok, EL_CMD_MAX_ARGS, &ntok) != EL_CMD_OK)
        return el_cmd_error(ctx, EL_CMD_ERR_SYNTAX, "syntax: unclosed quote or more than %d words", EL_CMD_MAX_ARGS);
    if (ntok == 0) return EL_CMD_OK;

    const el_cmd_def_t *best = NULL;
    int best_words = 0;
    for (size_t i = 0; i < n; i++) {
        int w = match_name(table[i].name, tok, ntok);
        if (w > best_words) { best = &table[i]; best_words = w; }
    }
    if (!best) return el_cmd_error(ctx, EL_CMD_ERR_UNKNOWN, "unknown command '%s', try HELP", tok[0]);
    if (ctx->level < best->min_level)
        return el_cmd_error(ctx, EL_CMD_ERR_DENIED, "%s requires %s login", best->name, el_level_name(best->min_level));

    int argc = ntok - best_words;
    if (argc < best->min_args || argc > best->max_args)
        return el_cmd_error(ctx, EL_CMD_ERR_ARGS, "usage: %s %s", best->name, best->usage ? best->usage : "");
    return best->fn(ctx, argc, tok + best_words);
}

void el_cmd_print_help(const el_cmd_def_t *table, size_t n, el_cmd_ctx_t *ctx)
{
    for (size_t i = 0; i < n; i++) {
        const el_cmd_def_t *d = &table[i];
        if (ctx->level < d->min_level) continue;
        el_cmd_reply(ctx, "%-14s %-22s %s%s", d->name, d->usage ? d->usage : "", d->help ? d->help : "",
                     d->min_level > EL_LEVEL_OPERATOR ? (d->min_level == EL_LEVEL_ADMIN ? " [admin]" : " [engineer]") : "");
    }
    el_cmd_reply(ctx, "END");
}

void el_line_init(el_line_t *l) { l->len = 0; l->overflow = false; l->buf[0] = '\0'; }

el_line_status_t el_line_feed(el_line_t *l, char c)
{
    if (c == '\r' || c == '\n') {
        bool over = l->overflow;
        l->buf[l->len] = '\0';
        l->len = 0;
        l->overflow = false;
        return over ? EL_LINE_OVERFLOW : EL_LINE_READY;
    }
    if (c == '\b' || c == 0x7F) {
        if (l->len && !l->overflow) l->len--;
        return EL_LINE_PENDING;
    }
    if (l->len < EL_CMD_LINE_MAX - 1) l->buf[l->len++] = c;
    else l->overflow = true;
    return EL_LINE_PENDING;
}
