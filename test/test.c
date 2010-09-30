#include <stdio.h>
#include "test.h"

int TEST_PRINT_PASS = 0;
int TEST_PRINT_FAIL = 1;

static int __STATUS     = 0;
static int __TESTS      = 0;
static int __ASSERTIONS = 0;
static int __FAILURES   = 0;

/**********************************************************/

static inline void __test_failed(void)
{
	++__FAILURES;
	__STATUS = 0;
}

/**********************************************************/

inline void test(const char *s)
{
	__STATUS = 1;
	++__TESTS;
	printf("%s\n", s);
}

int test_status(void)
{
	printf("\n"
	       "--------------------\n"
	       "TEST RESULTS SUMMARY\n"
	       "--------------------\n");
	printf("%4i test(s)\n"
	       "%4i assertion(s)\n"
	       "\n"
	       "%4i FAILURE(S)\n",
	       __TESTS, __ASSERTIONS, __FAILURES);

	return __FAILURES;
}

int test_setup(int argc, char **argv)
{
	/* only works with NULL-terminated argv */
	while (*(++argv)) {
		if (strcmp(*argv, "-v") == 0) {
			TEST_PRINT_PASS = 1;
			TEST_PRINT_FAIL = 1;
		} else if (strcmp(*argv, "-q") == 0) {
			TEST_PRINT_PASS = 0;
			TEST_PRINT_FAIL = 0;
		}
	}
}

/** ASSERTIONS **/

void assert_fail(const char *s)
{
	++__ASSERTIONS;
	__test_failed();
	if (TEST_PRINT_FAIL) { printf(" - %s: FAIL\n", s); }
}

void assert_pass(const char *s)
{
	++__ASSERTIONS;
	if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
}

void assert_true(const char *s, int value)
{
	++__ASSERTIONS;
	(value ? assert_pass(s) : assert_fail(s));
}

void assert_false(const char *s, int value)
{
	assert_true(s, !value);
}

void assert_not_null(const char *s, void *ptr)
{
	++__ASSERTIONS;
	if (ptr != NULL) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %p is NULL\n", s, ptr);
		}
	}
}

void assert_null(const char *s, void *ptr)
{
	++__ASSERTIONS;
	if (ptr == NULL) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %p is not NULL\n", s, ptr);
		}
	}
}

void assert_int_equals(const char *s, int expected, int actual)
{
	++__ASSERTIONS;
	if (expected == actual) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i != %i\n", s, expected, actual);
		}
	}
}

void assert_int_not_equal(const char *s, int unexpected, int actual)
{
	++__ASSERTIONS;
	if (unexpected == actual) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i == %i\n", s, unexpected, actual);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

void assert_str_equals(const char *s, const char *expected, const char *actual)
{
	++__ASSERTIONS;
	if (expected == NULL) { expected = "(null)"; }
	if (actual == NULL)   { actual   = "(null)"; }

	if (strcmp(expected, actual) == 0) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %s != %s\n", s, expected, actual);
		}
	}
}

void assert_str_not_equal(const char *s, const char *unexpected, const char *actual)
{
	++__ASSERTIONS;
	if (unexpected == NULL) { unexpected = "(null)"; }
	if (actual == NULL)     { actual     = "(null)"; }

	if (strcmp(unexpected, actual) == 0) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %s == %s\n", s, unexpected, actual);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}
