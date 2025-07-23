#include "../util.h"
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stddef.h>

static void test_isnum_positive(void **state) {
  (void)state; /* unused */
  assert_true(isnum("123"));
  assert_true(isnum("0"));
  assert_true(isnum("9876543210"));
}

static void test_isnum_negative(void **state) {
  (void)state; /* unused */
  assert_false(isnum("abc"));
  assert_false(isnum("12a"));
  assert_false(isnum("a12"));
  assert_false(isnum("-123"));
  assert_false(isnum("12.3"));
  assert_false(isnum(""));
  assert_false(isnum(NULL));
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_isnum_positive),
      cmocka_unit_test(test_isnum_negative),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
