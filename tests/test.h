#ifndef _OPENAGC_TEST_H_
#define _OPENAGC_TEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_test_pass;
extern int g_test_fail;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
        g_test_fail++; \
    } else { \
        g_test_pass++; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    TEST_ASSERT((a) == (b), msg)

#define TEST_ASSERT_NE(a, b, msg) \
    TEST_ASSERT((a) != (b), msg)

#define TEST_RUN(func) do { \
    printf("  %s ...\n", #func); \
    func(); \
} while (0)

#define TEST_SUITE(name) \
    printf("\n=== %s ===\n", name)

#define TEST_SUMMARY() do { \
    printf("\n--- Results: %d passed, %d failed ---\n", g_test_pass, g_test_fail); \
    if (g_test_fail > 0) exit(1); \
} while (0)

#endif /* _OPENAGC_TEST_H_ */
