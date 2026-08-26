#ifndef TEST_UTIL_H
#define TEST_UTIL_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_tests_run = 0, g_tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { g_tests_run++; printf("  %s\n", #name); name(); } while (0)
#define FAIL(msg) do { g_tests_failed++; printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return; } while (0)
#define ASSERT_TRUE(x) do { if (!(x)) FAIL("expected true: " #x); } while (0)
#define ASSERT_EQ_INT(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a != _b) { printf("    got %ld, want %ld\n", _a, _b); FAIL(#a " != " #b); } } while (0)
#define ASSERT_EQ_STR(a, b) do { const char *_a = (a), *_b = (b); if (!_a || !_b || strcmp(_a, _b) != 0) { printf("    got \"%s\", want \"%s\"\n", _a ? _a : "(null)", _b ? _b : "(null)"); FAIL(#a " != " #b); } } while (0)
#define TEST_MAIN_END() do { printf("%d tests, %d failed\n", g_tests_run, g_tests_failed); return g_tests_failed ? 1 : 0; } while (0)

#endif
