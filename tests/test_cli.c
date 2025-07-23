#include "../types.h"
#include "../util.h"
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// Test utility functions that are used by CLI parsing
static void test_isnum_positive_integration(void **state) {
    assert_true(isnum("123"));
    assert_true(isnum("0"));
    assert_true(isnum("9876543210"));
}

static void test_isnum_negative_integration(void **state) {
    assert_false(isnum("abc"));
    assert_false(isnum("12a"));
    assert_false(isnum("a12"));
    assert_false(isnum("-123"));
    assert_false(isnum("12.3"));
    assert_false(isnum(""));
    assert_false(isnum(NULL));
}

static void test_xstrdup_basic(void **state) {
    const char *original = "test string";
    char *duplicate = xstrdup(original);
    
    assert_non_null(duplicate);
    assert_string_equal(duplicate, original);
    assert_ptr_not_equal(duplicate, original);
    
    free(duplicate);
}

static void test_xstrdup_null(void **state) {
    char *duplicate = xstrdup(NULL);
    
    assert_null(duplicate);
}

static void test_xrealloc_basic(void **state) {
    void *ptr = malloc(10);
    assert_non_null(ptr);
    
    void *new_ptr = xrealloc(ptr, 20);
    assert_non_null(new_ptr);
    
    free(new_ptr);
}

static void test_xrealloc_null(void **state) {
    void *new_ptr = xrealloc(NULL, 20);
    assert_non_null(new_ptr);
    
    free(new_ptr);
}

static void test_memory_management(void **state) {
    // Test that memory allocation works correctly
    char *str1 = xstrdup("test1");
    char *str2 = xstrdup("test2");
    
    assert_non_null(str1);
    assert_non_null(str2);
    assert_string_equal(str1, "test1");
    assert_string_equal(str2, "test2");
    
    free(str1);
    free(str2);
}

static void test_string_operations(void **state) {
    // Test string operations used throughout the codebase
    char *empty = xstrdup("");
    char *null_str = xstrdup(NULL);
    
    assert_non_null(empty);
    assert_string_equal(empty, "");
    assert_null(null_str);
    
    free(empty);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isnum_positive_integration),
        cmocka_unit_test(test_isnum_negative_integration),
        cmocka_unit_test(test_xstrdup_basic),
        cmocka_unit_test(test_xstrdup_null),
        cmocka_unit_test(test_xrealloc_basic),
        cmocka_unit_test(test_xrealloc_null),
        cmocka_unit_test(test_memory_management),
        cmocka_unit_test(test_string_operations),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
} 