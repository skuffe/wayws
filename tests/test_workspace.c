#include "../util.h"
#include "../workspace.h"
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stddef.h>

// Mocking the die function to avoid exiting the test runner
void __wrap_die(const char *msg) { check_expected(msg); }

static void test_current_ws_no_output_name(void **state) {
  struct wayws_state s = {0};
  struct ws ws1 = {.name = "ws1", .active = 1, .last_active_seq = 1};
  struct ws ws2 = {.name = "ws2", .active = 0, .last_active_seq = 0};
  struct ws *vec[] = {&ws1, &ws2};
  s.vec = vec;
  s.vlen = 2;

  struct ws *current = current_ws(&s, NULL);
  assert_non_null(current);
  assert_string_equal(current->name, "ws1");
}

static void test_current_ws_with_output_name(void **state) {
  struct wayws_state s = {0};
  struct output out1 = {.name = "out1"};
  struct output out2 = {.name = "out2"};
  struct group_output go1 = {.output = &out1};
  struct group_output go2 = {.output = &out2};
  struct workspace_group g1 = {.outputs = &go1};
  struct workspace_group g2 = {.outputs = &go2};
  struct ws ws1 = {
      .name = "ws1", .active = 1, .last_active_seq = 1, .group = &g1};
  struct ws ws2 = {
      .name = "ws2", .active = 1, .last_active_seq = 2, .group = &g2};
  struct ws *vec[] = {&ws1, &ws2};
  s.vec = vec;
  s.vlen = 2;
  s.opt_output_name = "out2";

  struct ws *current = current_ws(&s, NULL);
  assert_non_null(current);
  assert_string_equal(current->name, "ws2");
}

static void test_current_ws_multi_output_group(void **state) {
  struct wayws_state s = {0};
  struct output out1 = {.name = "out1"};
  struct output out2 = {.name = "out2"};
  struct group_output go1 = {.output = &out1};
  struct group_output go2 = {.output = &out2};
  go1.next = &go2;
  struct workspace_group g1 = {.outputs = &go1};
  struct ws ws1 = {
      .name = "ws1", .active = 1, .last_active_seq = 1, .group = &g1};
  struct ws ws2 = {
      .name = "ws2", .active = 1, .last_active_seq = 2, .group = &g1};
  struct ws *vec[] = {&ws1, &ws2};
  s.vec = vec;
  s.vlen = 2;

  struct ws *current = current_ws(&s, NULL);
  assert_non_null(current);
  assert_string_equal(current->name, "ws2");
}

static void test_neighbor_right(void **state) {
  struct wayws_state s = {0};
  struct ws ws1 = {
      .name = "ws1", .index = 0, .active = 1, .last_active_seq = 1};
  struct ws ws2 = {.name = "ws2", .index = 1};
  struct ws ws3 = {.name = "ws3", .index = 2};
  struct ws *vec[] = {&ws1, &ws2, &ws3};
  s.vec = vec;
  s.vlen = 3;
  s.grid_cols = 2;
  struct workspace_group g = {0};
  ws1.group = &g;
  ws2.group = &g;
  ws3.group = &g;

  struct ws *n = neighbor(&s, DIR_RIGHT);
  assert_non_null(n);
  assert_string_equal(n->name, "ws2");
}

static void test_neighbor_left_edge(void **state) {
  struct wayws_state s = {0};
  struct ws ws1 = {
      .name = "ws1", .index = 0, .active = 1, .last_active_seq = 1};
  struct ws ws2 = {.name = "ws2", .index = 1};
  struct ws *vec[] = {&ws1, &ws2};
  s.vec = vec;
  s.vlen = 2;
  s.grid_cols = 2;
  struct workspace_group g = {0};
  ws1.group = &g;
  ws2.group = &g;

  struct ws *n = neighbor(&s, DIR_LEFT);
  assert_null(n);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_current_ws_no_output_name),
      cmocka_unit_test(test_current_ws_with_output_name),
      cmocka_unit_test(test_current_ws_multi_output_group),
      cmocka_unit_test(test_neighbor_right),
      cmocka_unit_test(test_neighbor_left_edge),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
