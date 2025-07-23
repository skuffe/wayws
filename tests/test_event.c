#include "../event.h"
#include "../types.h"
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// Mock output for testing
static char test_output[1024];
static size_t test_output_pos = 0;

// Mock printf to capture output
int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(test_output + test_output_pos, 
                          sizeof(test_output) - test_output_pos, 
                          format, args);
    test_output_pos += result;
    va_end(args);
    return result;
}

// Test setup and teardown
static int setup(void **state) {
    test_output_pos = 0;
    memset(test_output, 0, sizeof(test_output));
    return 0;
}

static int teardown(void **state) {
    return 0;
}

// Test event emission with different event types
static void test_emit_event_workspace_created(void **state) {
    struct wayws_state s = {.event_enabled = 1};
    
    emit_event(&s, EVENT_WORKSPACE_CREATED, "test-ws", "DP-1", 1, 0, 0, 1, 0, 0, DIR_NONE, NULL);
    
    assert_true(strstr(test_output, "\"type\":\"workspace_created\"") != NULL);
    assert_true(strstr(test_output, "\"name\":\"test-ws\"") != NULL);
    assert_true(strstr(test_output, "\"output\":\"DP-1\"") != NULL);
    assert_true(strstr(test_output, "\"active\":true") != NULL);
    assert_true(strstr(test_output, "\"timestamp\":") != NULL);
}

static void test_emit_event_workspace_state(void **state) {
    struct wayws_state s = {.event_enabled = 1};
    
    emit_event(&s, EVENT_WORKSPACE_STATE, "test-ws", "DP-1", 1, 10, 20, 1, 1, 0, DIR_NONE, NULL);
    
    assert_true(strstr(test_output, "\"type\":\"workspace_state\"") != NULL);
    assert_true(strstr(test_output, "\"x\":10") != NULL);
    assert_true(strstr(test_output, "\"y\":20") != NULL);
    assert_true(strstr(test_output, "\"urgent\":true") != NULL);
    assert_true(strstr(test_output, "\"hidden\":false") != NULL);
}

static void test_emit_event_grid_movement(void **state) {
    struct wayws_state s = {.event_enabled = 1};
    
    emit_event(&s, EVENT_GRID_MOVEMENT, "test-ws", "DP-1", 1, 0, 0, 1, 0, 0, DIR_RIGHT, NULL);
    
    assert_true(strstr(test_output, "\"type\":\"grid_movement\"") != NULL);
    assert_true(strstr(test_output, "\"direction\":\"right\"") != NULL);
}

static void test_emit_event_disabled(void **state) {
    struct wayws_state s = {.event_enabled = 0};
    
    emit_event(&s, EVENT_WORKSPACE_CREATED, "test-ws", "DP-1", 1, 0, 0, 1, 0, 0, DIR_NONE, NULL);
    
    assert_int_equal(test_output_pos, 0); // No output when disabled
}

static void test_emit_event_null_names(void **state) {
    struct wayws_state s = {.event_enabled = 1};
    
    emit_event(&s, EVENT_WORKSPACE_CREATED, NULL, NULL, 1, 0, 0, 1, 0, 0, DIR_NONE, NULL);
    
    assert_true(strstr(test_output, "\"name\":\"\"") != NULL);
    assert_true(strstr(test_output, "\"output\":\"\"") != NULL);
}

static void test_emit_event_exec_command(void **state) {
    struct wayws_state s = {.event_enabled = 1, .opt_exec = "echo test"};
    
    // This test verifies the exec command is called
    // In a real test, we'd mock system() to verify it's called
    emit_event(&s, EVENT_WORKSPACE_CREATED, "test-ws", "DP-1", 1, 0, 0, 1, 0, 0, DIR_NONE, NULL);
    
    assert_true(strstr(test_output, "\"type\":\"workspace_created\"") != NULL);
}

// Test get_output_name_for_workspace helper
static void test_get_output_name_for_workspace_valid(void **state) {
    struct output out = {.name = "DP-1"};
    struct group_output go = {.output = &out};
    struct workspace_group g = {.outputs = &go};
    struct ws w = {.group = &g};
    
    const char *name = get_output_name_for_workspace(&w);
    assert_string_equal(name, "DP-1");
}

static void test_get_output_name_for_workspace_null_output(void **state) {
    struct group_output go = {.output = NULL};
    struct workspace_group g = {.outputs = &go};
    struct ws w = {.group = &g};
    
    const char *name = get_output_name_for_workspace(&w);
    assert_string_equal(name, "(unknown)");
}

static void test_get_output_name_for_workspace_null_workspace(void **state) {
    const char *name = get_output_name_for_workspace(NULL);
    assert_string_equal(name, "(unknown)");
}

static void test_get_output_name_for_workspace_null_group(void **state) {
    struct ws w = {.group = NULL};
    
    const char *name = get_output_name_for_workspace(&w);
    assert_string_equal(name, "(unknown)");
}

static void test_get_output_name_for_workspace_null_outputs(void **state) {
    struct workspace_group g = {.outputs = NULL};
    struct ws w = {.group = &g};
    
    const char *name = get_output_name_for_workspace(&w);
    assert_string_equal(name, "(unknown)");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_emit_event_workspace_created, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emit_event_workspace_state, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emit_event_grid_movement, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emit_event_disabled, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emit_event_null_names, setup, teardown),
        cmocka_unit_test_setup_teardown(test_emit_event_exec_command, setup, teardown),
        cmocka_unit_test(test_get_output_name_for_workspace_valid),
        cmocka_unit_test(test_get_output_name_for_workspace_null_output),
        cmocka_unit_test(test_get_output_name_for_workspace_null_workspace),
        cmocka_unit_test(test_get_output_name_for_workspace_null_group),
        cmocka_unit_test(test_get_output_name_for_workspace_null_outputs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
} 