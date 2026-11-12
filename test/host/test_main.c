/* Single runner for every host test group. Groups register themselves: add
 * tests/test_<group>.c defining run_<group>_tests() and re-run tools\host_test.ps1. */
#include "unity.h"
#include "test_registry.h"

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    run_all_test_groups();
    return UNITY_END();
}
