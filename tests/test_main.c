/*
 * openagc test runner
 */

#include "test.h"

int g_test_pass = 0;
int g_test_fail = 0;

/* Test suite declarations */
void test_suite_types(void);
void test_suite_capabilities(void);
void test_suite_memory(void);
void test_suite_acb(void);
void test_suite_cb(void);
void test_suite_dcb(void);
void test_suite_driver(void);
void test_suite_texture(void);
void test_suite_shader(void);
void test_suite_graphics(void);
void test_suite_ioctl(void);
void test_suite_register_defaults(void);
void test_suite_registers(void);
void test_suite_sony_exports(void);
void test_suite_driver_registry(void);
void test_suite_videoout(void);

int main(void) {
    printf("openagc test suite\n");

    test_suite_types();
    test_suite_capabilities();
    test_suite_memory();
    test_suite_acb();
    test_suite_cb();
    test_suite_dcb();
    test_suite_driver();
    test_suite_texture();
    test_suite_shader();
    test_suite_graphics();
    test_suite_ioctl();
    test_suite_register_defaults();
    test_suite_registers();
    test_suite_sony_exports();
    test_suite_driver_registry();
    test_suite_videoout();

    TEST_SUMMARY();
    return 0;
}
