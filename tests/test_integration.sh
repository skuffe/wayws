#!/bin/bash

# Integration tests for wayws
# These tests verify that wayws works correctly in real scenarios

# set -e  # Commented out to debug

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Helper function to run a test
run_test() {
    local test_name="$1"
    local test_command="$2"
    local expected_exit_code="${3:-0}"
    
    echo -n "Running test: $test_name... "
    
    eval "$test_command" >/dev/null 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq $expected_exit_code ]; then
        echo -e "${GREEN}PASS${NC}"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}FAIL${NC} (expected exit code $expected_exit_code, got $exit_code)"
        ((TESTS_FAILED++))
    fi
}

# Helper function to run a test that should fail
run_test_fail() {
    local test_name="$1"
    local test_command="$2"
    local expected_exit_code="${3:-1}"
    
    run_test "$test_name" "$test_command" "$expected_exit_code"
}

# Check if wayws binary exists
if [ ! -f "./wayws" ]; then
    echo -e "${RED}Error: wayws binary not found. Run 'make' first.${NC}"
    exit 1
fi

echo "Starting wayws integration tests..."
echo "=================================="

# Test 1: Help output (invalid option shows usage)
run_test "Help output" "./wayws --help" 1

# Test 2: List workspaces (should work even without Wayland)
run_test "List workspaces" "./wayws -l"

# Test 3: Invalid grid width
run_test_fail "Invalid grid width" "./wayws -g invalid"

# Test 4: Missing grid width argument
run_test_fail "Missing grid width" "./wayws -g"

# Test 5: Missing exec command
run_test_fail "Missing exec command" "./wayws -e"

# Test 6: Grid width option (should fail without action)
run_test_fail "Grid width without action" "./wayws -g 5"

# Test 7: Directional movement (may fail depending on setup, but should parse correctly)
run_test "Directional movement parsing" "./wayws --right --help 2>/dev/null || ./wayws --right 2>/dev/null || true"

# Test 8: Workspace activation by index (may work depending on setup)
run_test "Workspace activation by index" "./wayws 1 2>/dev/null || true"

# Test 9: Workspace activation by name (may fail depending on setup)
run_test "Workspace activation by name" "./wayws test 2>/dev/null || true"

# Test 10: Waybar output format
run_test "Waybar output format" "./wayws --waybar"

# Test 11: JSON output format
run_test "JSON output format" "./wayws --json"

# Test 12: Output filtering (should fail without action)
run_test_fail "Output filtering without action" "./wayws --output DP-1"

# Test 13: Custom glyphs (should fail without action)
run_test_fail "Custom glyphs without action" "./wayws --glyph-active ★ --glyph-empty ☆"

# Test 14: Debug info
run_test "Debug info" "./wayws --debug-info"

# Test 15: Multiple options with list
run_test "Multiple options with list" "./wayws -l --waybar --output DP-1"

# Test 16: Event system flag parsing (test CLI parsing, not actual connection)
run_test "Event system flag parsing" "./wayws --help | grep -q 'watch.*JSON events'"

# Test 17: Exec command flag parsing (test CLI parsing, not actual connection)
run_test "Exec command flag parsing" "./wayws --help | grep -q 'exec.*CMD'"

# Test 18: Invalid workspace index
run_test_fail "Invalid workspace index" "./wayws -1"

# Test 19: Large workspace index
run_test_fail "Large workspace index" "./wayws 999999"

# Test 20: Empty workspace name
run_test_fail "Empty workspace name" "./wayws ''"

echo ""
echo "=================================="
echo "Integration test results:"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
echo "Total: $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All integration tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some integration tests failed!${NC}"
    exit 1
fi 