#include <string.h>
#include "unity.h"
#include "el_cmd.h"

/* Captured console output and the last handler call. */
static char out[2048];
static int  called_argc;
static char called_args[EL_CMD_MAX_ARGS][32];
static const char *called;

static void sink(void *user, const char *text) { (void)user; strncat(out, text, sizeof out - strlen(out) - 1); }

static el_cmd_err_t record(const char *who, int argc, char **argv)
{
    called = who; called_argc = argc;
    for (int i = 0; i < argc; i++) strncpy(called_args[i], argv[i], sizeof called_args[i] - 1);
    return EL_CMD_OK;
}
static el_cmd_err_t h_log(el_cmd_ctx_t *c, int n, char **a)       { (void)c; return record("LOG?", n, a); }
static el_cmd_err_t h_log_count(el_cmd_ctx_t *c, int n, char **a) { (void)c; return record("LOG COUNT?", n, a); }
static el_cmd_err_t h_sample(el_cmd_ctx_t *c, int n, char **a)    { (void)c; return record("SAMPLE", n, a); }
static el_cmd_err_t h_io_set(el_cmd_ctx_t *c, int n, char **a)    { record("IO SET", n, a); el_cmd_reply(c, "OK"); return EL_CMD_OK; }
static el_cmd_err_t h_cal(el_cmd_ctx_t *c, int n, char **a)       { (void)c; return record("CAL ZERO", n, a); }
static el_cmd_err_t h_busy(el_cmd_ctx_t *c, int n, char **a)      { (void)n; (void)a; return el_cmd_error(c, EL_CMD_ERR_BUSY, "measurement running"); }

static const el_cmd_def_t TABLE[] = {
    { "LOG?",       EL_LEVEL_OPERATOR, 1, 1, h_log,       "<n>",          "last n results" },
    { "LOG COUNT?", EL_LEVEL_OPERATOR, 0, 0, h_log_count, "",             "number of results" },
    { "SAMPLE",     EL_LEVEL_OPERATOR, 1, 1, h_sample,    "<id>",         "sample ID for next result" },
    { "CAL ZERO",   EL_LEVEL_ADMIN,    0, 0, h_cal,       "",             "zero calibration" },
    { "IO SET",     EL_LEVEL_ENGINEER, 2, 2, h_io_set,    "<line> <0|1>", "drive an output" },
    { "MEAS",       EL_LEVEL_OPERATOR, 0, 0, h_busy,      "",             "start a measurement" },
};
#define N (sizeof TABLE / sizeof TABLE[0])

static el_cmd_ctx_t ctx;

static el_cmd_err_t run(const char *text, el_level_t level)
{
    char line[EL_CMD_LINE_MAX];
    strncpy(line, text, sizeof line - 1); line[sizeof line - 1] = '\0';
    out[0] = '\0'; called = NULL; called_argc = -1;
    memset(called_args, 0, sizeof called_args);
    ctx.out = sink; ctx.user = NULL; ctx.level = level;
    return el_cmd_execute(TABLE, N, line, &ctx);
}

static void test_dispatch_and_case_insensitive(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_OK, run("sample S-0001\r\n", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL_STRING("SAMPLE", called);
    TEST_ASSERT_EQUAL(1, called_argc);
    TEST_ASSERT_EQUAL_STRING("S-0001", called_args[0]);       /* argument case preserved */
}

static void test_longest_name_wins(void)
{
    run("LOG COUNT?", EL_LEVEL_OPERATOR);
    TEST_ASSERT_EQUAL_STRING("LOG COUNT?", called);
    run("LOG? 5", EL_LEVEL_OPERATOR);
    TEST_ASSERT_EQUAL_STRING("LOG?", called);
    TEST_ASSERT_EQUAL_STRING("5", called_args[0]);
}

static void test_quoted_argument_keeps_spaces(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_OK, run("SAMPLE \"Tank 3 port\"", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL_STRING("Tank 3 port", called_args[0]);
    TEST_ASSERT_EQUAL(EL_CMD_ERR_SYNTAX, run("SAMPLE \"unclosed", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL(EL_CMD_ERR_SYNTAX, run("SAMPLE \"a\"b", EL_LEVEL_OPERATOR));
    TEST_ASSERT_NULL(called);
}

static void test_unknown_and_blank(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_ERR_UNKNOWN, run("FROB", EL_LEVEL_ENGINEER));
    TEST_ASSERT_EQUAL_STRING("ERR 1 unknown command 'FROB', try HELP\n", out);
    TEST_ASSERT_EQUAL(EL_CMD_OK, run("   \r\n", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL_STRING("", out);                         /* blank line: silent */
    TEST_ASSERT_EQUAL(EL_CMD_ERR_UNKNOWN, run("LOG", EL_LEVEL_OPERATOR));   /* partial name */
}

static void test_access_levels_enforced(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_ERR_DENIED, run("CAL ZERO", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL_STRING("ERR 4 CAL ZERO requires ADMIN login\n", out);
    TEST_ASSERT_NULL(called);
    TEST_ASSERT_EQUAL(EL_CMD_OK, run("CAL ZERO", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_CMD_ERR_DENIED, run("IO SET DO1 1", EL_LEVEL_ADMIN));
    TEST_ASSERT_EQUAL(EL_CMD_OK, run("io set do1 1", EL_LEVEL_ENGINEER));
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
}

static void test_argument_count_checked_before_handler(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_ERR_ARGS, run("IO SET DO1", EL_LEVEL_ENGINEER));
    TEST_ASSERT_EQUAL_STRING("ERR 3 usage: IO SET <line> <0|1>\n", out);
    TEST_ASSERT_EQUAL(EL_CMD_ERR_ARGS, run("LOG COUNT? 3", EL_LEVEL_OPERATOR));
    TEST_ASSERT_NULL(called);
}

static void test_handler_error_passes_through(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_ERR_BUSY, run("MEAS", EL_LEVEL_OPERATOR));
    TEST_ASSERT_EQUAL_STRING("ERR 5 measurement running\n", out);
}

static void test_too_many_words(void)
{
    TEST_ASSERT_EQUAL(EL_CMD_ERR_SYNTAX, run("SAMPLE a b c d e f g h i j k", EL_LEVEL_OPERATOR));
}

static void test_help_hides_commands_above_level(void)
{
    out[0] = '\0'; ctx.out = sink; ctx.level = EL_LEVEL_OPERATOR;
    el_cmd_print_help(TABLE, N, &ctx);
    TEST_ASSERT_NOT_NULL(strstr(out, "SAMPLE"));
    TEST_ASSERT_NULL(strstr(out, "CAL ZERO"));
    TEST_ASSERT_NULL(strstr(out, "IO SET"));
    TEST_ASSERT_NOT_NULL(strstr(out, "END\n"));
    out[0] = '\0'; ctx.level = EL_LEVEL_ENGINEER;
    el_cmd_print_help(TABLE, N, &ctx);
    TEST_ASSERT_NOT_NULL(strstr(out, "[engineer]"));
    TEST_ASSERT_NOT_NULL(strstr(out, "[admin]"));
}

static void test_long_reply_is_truncated_but_terminated(void)
{
    char big[400]; memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = '\0';
    out[0] = '\0'; ctx.out = sink;
    el_cmd_reply(&ctx, "%s", big);
    size_t len = strlen(out);
    TEST_ASSERT_TRUE(len < 256);
    TEST_ASSERT_EQUAL('\n', out[len - 1]);
}

static void test_line_assembly(void)
{
    el_line_t l; el_line_init(&l);
    const char *in = "MEAZ\bS\r\n";
    el_line_status_t st = EL_LINE_PENDING;
    for (const char *p = in; *p && st != EL_LINE_READY; p++) st = el_line_feed(&l, *p);
    TEST_ASSERT_EQUAL(EL_LINE_READY, st);
    TEST_ASSERT_EQUAL_STRING("MEAS", l.buf);
    TEST_ASSERT_EQUAL(EL_LINE_READY, el_line_feed(&l, '\n'));   /* the \n of \r\n: empty line */
    TEST_ASSERT_EQUAL_STRING("", l.buf);
}

static void test_line_overflow_discards_whole_line(void)
{
    el_line_t l; el_line_init(&l);
    for (int i = 0; i < EL_CMD_LINE_MAX + 20; i++) TEST_ASSERT_EQUAL(EL_LINE_PENDING, el_line_feed(&l, 'A'));
    TEST_ASSERT_EQUAL(EL_LINE_OVERFLOW, el_line_feed(&l, '\n'));
    el_line_feed(&l, 'O'); el_line_feed(&l, 'K');
    TEST_ASSERT_EQUAL(EL_LINE_READY, el_line_feed(&l, '\n'));    /* next line is clean */
    TEST_ASSERT_EQUAL_STRING("OK", l.buf);
}

void run_cmd_tests(void)
{
    RUN_TEST(test_dispatch_and_case_insensitive);
    RUN_TEST(test_longest_name_wins);
    RUN_TEST(test_quoted_argument_keeps_spaces);
    RUN_TEST(test_unknown_and_blank);
    RUN_TEST(test_access_levels_enforced);
    RUN_TEST(test_argument_count_checked_before_handler);
    RUN_TEST(test_handler_error_passes_through);
    RUN_TEST(test_too_many_words);
    RUN_TEST(test_help_hides_commands_above_level);
    RUN_TEST(test_long_reply_is_truncated_but_terminated);
    RUN_TEST(test_line_assembly);
    RUN_TEST(test_line_overflow_discards_whole_line);
}
