#include <string.h>
#include "unity.h"
#include "el_store.h"
#include "el_util.h"

/* Builds the first bytes of an ESP-IDF app image the way esptool/elf2image lays them out. */
static void make_image(uint8_t *b, const char *project, const char *version, uint16_t chip)
{
    memset(b, 0, EL_STORE_FW_PROBE_SIZE);
    b[0] = 0xE9;                         /* ESP_IMAGE_HEADER_MAGIC */
    b[1] = 5;                            /* segments */
    b[2] = 2; b[3] = 0x4F;               /* spi mode / speed+size: not checked */
    el_put_u32le(b + 4, 0x40375A88u);    /* entry */
    el_put_u16le(b + 12, chip);
    b[23] = 1;                           /* SHA-256 appended */
    el_put_u32le(b + 24, 0x3C0A0020u);   /* seg 0 load address (DROM) */
    el_put_u32le(b + 28, 0x1F234u);      /* seg 0 length */
    el_put_u32le(b + 32, 0xABCD5432u);   /* ESP_APP_DESC_MAGIC_WORD */
    el_put_u32le(b + 36, 0);
    strncpy((char *)b + 48, version, 32);
    strncpy((char *)b + 80, project, 32);
    memcpy(b + 112, "12:00:00", 8);
    memcpy(b + 128, "Oct 30 2026", 11);
    memcpy(b + 144, "v5.5.6", 6);
}

static el_store_fw_policy_t policy(void)
{
    el_store_fw_policy_t p = {
        .project = "envlogger", .running_version = "0.2.0", .chip_id = EL_STORE_FW_CHIP_ESP32S3,
        .slot_bytes = 0x400000, .allow_downgrade = false,
    };
    return p;
}

static void test_fw_parse_good_image(void)
{
    uint8_t b[EL_STORE_FW_PROBE_SIZE];
    el_store_fw_info_t i;
    make_image(b, "envlogger", "0.3.1", 9);
    TEST_ASSERT_EQUAL(EL_OK, el_store_fw_parse(b, sizeof b, &i));
    TEST_ASSERT_EQUAL_UINT8(5, i.segments);
    TEST_ASSERT_EQUAL_UINT16(9, i.chip_id);
    TEST_ASSERT_TRUE(i.hash_appended);
    TEST_ASSERT_EQUAL_STRING("0.3.1", i.version);
    TEST_ASSERT_EQUAL_STRING("envlogger", i.project);
    TEST_ASSERT_EQUAL_STRING("12:00:00", i.time);
    TEST_ASSERT_EQUAL_STRING("Oct 30 2026", i.date);
    TEST_ASSERT_EQUAL_STRING("v5.5.6", i.idf_ver);
}

static void test_fw_parse_unterminated_fields(void)
{
    uint8_t b[EL_STORE_FW_PROBE_SIZE];
    el_store_fw_info_t i;
    make_image(b, "p", "v", 9);
    memset(b + 48, 'A', 32);             /* version fills all 32 bytes, no NUL */
    memset(b + 80, 'B', 32);
    TEST_ASSERT_EQUAL(EL_OK, el_store_fw_parse(b, sizeof b, &i));
    TEST_ASSERT_EQUAL_UINT(32, strlen(i.version));
    TEST_ASSERT_EQUAL_UINT(32, strlen(i.project));
}

static void test_fw_parse_rejects(void)
{
    uint8_t b[EL_STORE_FW_PROBE_SIZE];
    el_store_fw_info_t i;
    make_image(b, "envlogger", "0.3.1", 9);
    TEST_ASSERT_EQUAL(EL_ERR_SIZE, el_store_fw_parse(b, sizeof b - 1, &i));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_fw_parse(NULL, sizeof b, &i));
    TEST_ASSERT_EQUAL(EL_ERR_ARG, el_store_fw_parse(b, sizeof b, NULL));
    b[0] = 0xE8;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_fw_parse(b, sizeof b, &i));     /* a CSV renamed .bin */
    make_image(b, "envlogger", "0.3.1", 9); b[1] = 0;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_fw_parse(b, sizeof b, &i));
    make_image(b, "envlogger", "0.3.1", 9); b[1] = 17;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_fw_parse(b, sizeof b, &i));
    make_image(b, "envlogger", "0.3.1", 9); el_put_u32le(b + 28, 255);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_fw_parse(b, sizeof b, &i));     /* no room for desc */
    make_image(b, "envlogger", "0.3.1", 9); b[32] ^= 1;
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_fw_parse(b, sizeof b, &i));     /* bootloader image etc. */
    memset(b, 0xFF, sizeof b);
    TEST_ASSERT_EQUAL(EL_ERR_FORMAT, el_store_fw_parse(b, sizeof b, &i));
}

static void test_version_cmp(void)
{
    TEST_ASSERT_EQUAL_INT(0, el_store_version_cmp("1.2.3", "1.2.3"));
    TEST_ASSERT_EQUAL_INT(0, el_store_version_cmp("v1.2.3", "1.2.3"));
    TEST_ASSERT_EQUAL_INT(0, el_store_version_cmp("1.2", "1.2.0"));
    TEST_ASSERT_TRUE(el_store_version_cmp("1.10.0", "1.9.9") > 0);          /* numeric, not text */
    TEST_ASSERT_TRUE(el_store_version_cmp("0.1.0", "0.2.0") < 0);
    TEST_ASSERT_TRUE(el_store_version_cmp("2.0.0", "1.99.99") > 0);
    TEST_ASSERT_TRUE(el_store_version_cmp("1.2.0", "1.2.0-rc1") > 0);      /* release > pre-release */
    TEST_ASSERT_TRUE(el_store_version_cmp("0.1.0-skeleton", "0.1.0") < 0);
    TEST_ASSERT_TRUE(el_store_version_cmp("1.2.0-rc2", "1.2.0-rc1") > 0);
    TEST_ASSERT_TRUE(el_store_version_cmp("1.2.0.1", "1.2.0") > 0);        /* fourth number */
    TEST_ASSERT_TRUE(el_store_version_cmp("1.2.3", "dev") < 0);            /* "1" < "d" as text */
    TEST_ASSERT_EQUAL_INT(0, el_store_version_cmp("dev", "dev"));
    TEST_ASSERT_TRUE(el_store_version_cmp(NULL, "1.0") < 0);
    TEST_ASSERT_TRUE(el_store_version_cmp("1.0", NULL) > 0);
    TEST_ASSERT_EQUAL_INT(0, el_store_version_cmp(NULL, NULL));
    TEST_ASSERT_TRUE(el_store_version_cmp("99999999999.0", "4294967295.1") < 0);   /* saturates */
    TEST_ASSERT_TRUE(el_store_version_cmp("1.", "1.0") < 0);                /* "." is a suffix */
}

static void test_fw_check_policy(void)
{
    uint8_t b[EL_STORE_FW_PROBE_SIZE];
    el_store_fw_info_t i;
    el_store_fw_policy_t p = policy();
    make_image(b, "envlogger", "0.3.0", 9);
    el_store_fw_parse(b, sizeof b, &i);
    TEST_ASSERT_EQUAL(EL_STORE_FW_OK, el_store_fw_check(&i, 1200000, &p));
    TEST_ASSERT_EQUAL(EL_STORE_FW_OK, el_store_fw_check(&i, 0x400000, &p));      /* exactly fills */
    TEST_ASSERT_EQUAL(EL_STORE_FW_TOO_BIG, el_store_fw_check(&i, 0x400001, &p));
    TEST_ASSERT_EQUAL(EL_STORE_FW_BAD_IMAGE, el_store_fw_check(&i, 100, &p));
    TEST_ASSERT_EQUAL(EL_STORE_FW_BAD_IMAGE, el_store_fw_check(NULL, 1200000, &p));
    TEST_ASSERT_EQUAL(EL_STORE_FW_BAD_IMAGE, el_store_fw_check(&i, 1200000, NULL));

    p.chip_id = 0;                                                               /* ESP32 image */
    TEST_ASSERT_EQUAL(EL_STORE_FW_WRONG_CHIP, el_store_fw_check(&i, 1200000, &p));
    p = policy();
    p.project = "envlogger2";
    TEST_ASSERT_EQUAL(EL_STORE_FW_WRONG_PROJECT, el_store_fw_check(&i, 1200000, &p));

    p = policy();
    p.running_version = "0.3.0";
    TEST_ASSERT_EQUAL(EL_STORE_FW_OK, el_store_fw_check(&i, 1200000, &p));        /* re-flash same */
    p.running_version = "0.4.0";
    TEST_ASSERT_EQUAL(EL_STORE_FW_DOWNGRADE, el_store_fw_check(&i, 1200000, &p));
    p.allow_downgrade = true;                                                     /* ENGINEER */
    TEST_ASSERT_EQUAL(EL_STORE_FW_OK, el_store_fw_check(&i, 1200000, &p));
    /* first failure wins: wrong project reported before downgrade */
    p.allow_downgrade = false; p.project = "other";
    TEST_ASSERT_EQUAL(EL_STORE_FW_WRONG_PROJECT, el_store_fw_check(&i, 1200000, &p));
}

static void test_fw_verdict_names(void)
{
    TEST_ASSERT_EQUAL_STRING("OK", el_store_fw_verdict_name(EL_STORE_FW_OK));
    TEST_ASSERT_EQUAL_STRING("DOWNGRADE", el_store_fw_verdict_name(EL_STORE_FW_DOWNGRADE));
    TEST_ASSERT_EQUAL_STRING("?", el_store_fw_verdict_name(EL_STORE_FW_VERDICT_COUNT));
}

void run_store_fw_tests(void)
{
    RUN_TEST(test_fw_parse_good_image);
    RUN_TEST(test_fw_parse_unterminated_fields);
    RUN_TEST(test_fw_parse_rejects);
    RUN_TEST(test_version_cmp);
    RUN_TEST(test_fw_check_policy);
    RUN_TEST(test_fw_verdict_names);
}
