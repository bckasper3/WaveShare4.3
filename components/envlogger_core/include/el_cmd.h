#pragma once
/* Line-based command console (USB serial today; any byte stream later: RS-485, BLE, telnet).
 * The core does line assembly, tokenising, command lookup, access-level checks, argument
 * counts and error replies. Commands themselves live in a table supplied by the firmware
 * (or by the host simulator), so the device and the PC test harness share this exact code.
 *
 * Reply rules (docs/PROTOCOLS.md section 1): every reply line ends in '\n'; errors are
 * "ERR <code> <text>" with the stable numeric codes below. */
#include <stdbool.h>
#include <stddef.h>
#include "el_access.h"

#define EL_CMD_LINE_MAX  160   /* bytes including NUL */
#define EL_CMD_MAX_ARGS  10    /* tokens per line, command words included */

typedef enum {
    EL_CMD_OK = 0,
    EL_CMD_ERR_UNKNOWN = 1,        /* no such command */
    EL_CMD_ERR_SYNTAX = 2,         /* unterminated quote, too many tokens */
    EL_CMD_ERR_ARGS = 3,           /* wrong number or format of arguments */
    EL_CMD_ERR_DENIED = 4,         /* needs a higher access level */
    EL_CMD_ERR_BUSY = 5,           /* e.g. measurement or export running */
    EL_CMD_ERR_NO_DATA = 6,        /* e.g. no result yet */
    EL_CMD_ERR_FAILED = 7,         /* hardware or operation failed */
    EL_CMD_ERR_NOT_SUPPORTED = 8,  /* not available in this build / mode */
    EL_CMD_ERR_TOO_LONG = 9,       /* line longer than EL_CMD_LINE_MAX - 1 */
    EL_CMD_ERR_LOCKED = 10,        /* login locked out */
} el_cmd_err_t;

typedef void (*el_cmd_out_fn)(void *user, const char *text);

typedef struct {
    el_cmd_out_fn out;        /* writes text (already contains '\n') to the interface */
    void         *user;
    el_level_t    level;      /* caller copies its session level in before each line */
} el_cmd_ctx_t;

/* argv[0..argc-1] are the arguments after the command words. Strings point into the line
 * buffer and are valid only during the call. Return EL_CMD_OK or the error you reported. */
typedef el_cmd_err_t (*el_cmd_handler_t)(el_cmd_ctx_t *ctx, int argc, char **argv);

typedef struct {
    const char      *name;       /* one or more upper-case words, e.g. "LOG COUNT?", "IO SET" */
    el_level_t       min_level;
    unsigned char    min_args, max_args;
    el_cmd_handler_t fn;
    const char      *usage;      /* argument synopsis, e.g. "<DO1|BL> <0|1>", "" if none */
    const char      *help;       /* one line */
} el_cmd_def_t;

/* Run one line (modified in place). Words match case-insensitively; with several matches the
 * longest command name wins ("LOG COUNT?" beats "LOG"). Blank lines do nothing. */
el_cmd_err_t el_cmd_execute(const el_cmd_def_t *table, size_t n, char *line, el_cmd_ctx_t *ctx);

/* Output helpers for handlers: one line each, '\n' appended. */
void         el_cmd_reply(el_cmd_ctx_t *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
el_cmd_err_t el_cmd_error(el_cmd_ctx_t *ctx, el_cmd_err_t code, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* "HELP": the commands the caller's level may use, one per line, then "END". */
void el_cmd_print_help(const el_cmd_def_t *table, size_t n, el_cmd_ctx_t *ctx);

/* ---- Line assembly from a byte stream ---- */
typedef struct {
    char   buf[EL_CMD_LINE_MAX];
    size_t len;
    bool   overflow;
} el_line_t;

typedef enum { EL_LINE_PENDING = 0, EL_LINE_READY, EL_LINE_OVERFLOW } el_line_status_t;

void el_line_init(el_line_t *l);
/* Feed one byte. '\r' or '\n' ends a line; backspace/DEL edits (for PuTTY-style terminals).
 * READY: l->buf holds the NUL-terminated line until the next feed.
 * OVERFLOW: the line was too long and was discarded; reply ERR 9. */
el_line_status_t el_line_feed(el_line_t *l, char c);
