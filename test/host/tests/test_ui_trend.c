#include <math.h>
#include "unity.h"
#include "el_ui_trend.h"

static void test_short_series_is_copied(void)
{
    el_ui_pt_t in[3] = { { 1, 10.0f }, { 2, 20.0f }, { 3, 30.0f } }, out[8];
    TEST_ASSERT_EQUAL_size_t(3, el_ui_downsample(in, 3, out, 8));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out[1].v);
    TEST_ASSERT_EQUAL_size_t(0, el_ui_downsample(NULL, 3, out, 8));
    TEST_ASSERT_EQUAL_size_t(0, el_ui_downsample(in, 3, out, 0));
    TEST_ASSERT_EQUAL_size_t(1, el_ui_downsample(in, 3, out, 1));
    TEST_ASSERT_EQUAL_FLOAT(30.0f, out[0].v);                      /* the latest value */
    TEST_ASSERT_EQUAL_size_t(2, el_ui_downsample(in, 3, out, 2));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, out[0].v); TEST_ASSERT_EQUAL_FLOAT(30.0f, out[1].v);
}

/* Six months of daily readings in a warehouse: slow seasonal drift plus one spike. The spike
 * must survive downsampling to the chart's 60 points, and order/endpoints must be kept. */
static void test_spike_survives_and_order_is_kept(void)
{
    static el_ui_pt_t in[180], out[60];
    for (int i = 0; i < 180; i++) {
        in[i].t = 1777000000 + (int64_t)i * 86400;
        in[i].v = 200.0f + 1.5f * (float)i + 8.0f * sinf((float)i);
    }
    in[97].v = 1450.0f;
    size_t n = el_ui_downsample(in, 180, out, 60);
    TEST_ASSERT_EQUAL_size_t(60, n);
    TEST_ASSERT_EQUAL_INT64(in[0].t, out[0].t);
    TEST_ASSERT_EQUAL_INT64(in[179].t, out[59].t);
    bool spike = false;
    for (size_t i = 0; i < n; i++) {
        if (i) TEST_ASSERT_TRUE(out[i].t > out[i - 1].t);
        if (out[i].v == 1450.0f) spike = true;
    }
    TEST_ASSERT_TRUE(spike);
}

static void test_non_finite_values_are_not_chosen(void)
{
    static el_ui_pt_t in[40], out[10];
    for (int i = 0; i < 40; i++) { in[i].t = i; in[i].v = (i % 2) ? NAN : (float)i; }
    size_t n = el_ui_downsample(in, 40, out, 10);
    TEST_ASSERT_EQUAL_size_t(10, n);
    for (size_t i = 1; i + 1 < n; i++) TEST_ASSERT_TRUE(isfinite(out[i].v));
}

static void test_nice_range(void)
{
    float lo, hi, step;
    el_ui_nice_range(18.2f, 26.7f, 5, 1.0f, &lo, &hi, &step);     /* office day, limits folded in */
    TEST_ASSERT_TRUE(lo <= 18.2f && hi >= 26.7f);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, step);                            /* 2 would end at 26 < 27.1 */
    TEST_ASSERT_EQUAL_FLOAT(17.5f, lo); TEST_ASSERT_EQUAL_FLOAT(30.0f, hi);
    el_ui_nice_range(-21.0f, -16.5f, 5, 1.0f, &lo, &hi, &step);   /* freezer: negative axis */
    TEST_ASSERT_TRUE(lo <= -21.0f && hi >= -16.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fmodf(lo, step));
    el_ui_nice_range(22.0f, 22.0f, 4, 1.0f, &lo, &hi, &step);     /* flat series: at least 1 degC */
    TEST_ASSERT_TRUE(lo <= 21.5f && hi >= 22.5f);
    TEST_ASSERT_TRUE(hi - lo <= 4.0f);
    el_ui_nice_range(80.0f, 20.0f, 5, 1.0f, &lo, &hi, &step);     /* swapped inputs */
    TEST_ASSERT_TRUE(lo <= 20.0f && hi >= 80.0f);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, step);
    el_ui_nice_range(NAN, 5.0f, 0, 0.0f, &lo, &hi, NULL);          /* bad input -> about 0..10 */
    TEST_ASSERT_TRUE(lo <= 0.0f && hi >= 10.0f);
    for (int i = 0; i < 200; i++) {                                 /* the grid always covers */
        float a = -40.0f + (float)i * 0.83f, b2 = a + (float)(i % 17) * 1.7f;
        el_ui_nice_range(a, b2, 5, 1.0f, &lo, &hi, &step);
        TEST_ASSERT_TRUE(lo <= a && hi >= b2);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, step * 5.0f, hi - lo);
    }
}

void run_ui_trend_tests(void)
{
    RUN_TEST(test_short_series_is_copied);
    RUN_TEST(test_spike_survives_and_order_is_kept);
    RUN_TEST(test_non_finite_values_are_not_chosen);
    RUN_TEST(test_nice_range);
}
