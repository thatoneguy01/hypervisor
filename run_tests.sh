#!/usr/bin/env bash

set -u
set -o pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

SRC_FILE="${1:-hypervisor.c}"
TEST_DIR="./test"
HV="./hv"

if [[ ! -f "$SRC_FILE" ]]; then
    echo "ERROR: source file not found: $SRC_FILE"
    echo "Usage: $0 [source_file]"
    exit 1
fi

echo "Compiling $SRC_FILE..."
if ! gcc -std=c2x -o "$HV" "$SRC_FILE"; then
    echo "COMPILATION FAILED"
    exit 1
fi

echo

passed=0
failed=0
total=0

snapshot="./test/test_snapshot.bin"
if python3 "./test/create_snapshot.py" "$snapshot" &&
   "$HV" -s "$snapshot" > "./test/test_snapshot.actual" &&
   diff -u "./test/test_snapshot.expected" "./test/test_snapshot.actual" > /dev/null; then
    echo "[PASS] test_snapshot"
    passed=$((passed + 1))
    rm -f "./test/test_snapshot.actual"
else
    echo "[FAIL] test_snapshot"
    if [[ -f "./test/test_snapshot.actual" ]]; then
        diff -u "./test/test_snapshot.expected" "./test/test_snapshot.actual"
    fi
    failed=$((failed + 1))
fi
total=$((total + 1))

if "$HV" -s "$snapshot" -v "./test/test00_initial.conf" > "./test/test_snapshot_mixed.actual" &&
   cat "./test/test_snapshot.expected" "./test/test00_initial.expected" > "./test/test_snapshot_mixed.expected" &&
   diff -u "./test/test_snapshot_mixed.expected" "./test/test_snapshot_mixed.actual" > /dev/null; then
    echo "[PASS] test_snapshot_mixed_with_config"
    passed=$((passed + 1))
else
    echo "[FAIL] test_snapshot_mixed_with_config"
    if [[ -f "./test/test_snapshot_mixed.actual" ]]; then
        diff -u "./test/test_snapshot_mixed.expected" "./test/test_snapshot_mixed.actual"
    fi
    failed=$((failed + 1))
fi
total=$((total + 1))
rm -f "$snapshot" "./test/test_snapshot_mixed.actual" "./test/test_snapshot_mixed.expected"

for config in "$TEST_DIR"/*.conf; do
    [[ -e "$config" ]] || continue

    total=$((total + 1))

    base="${config%.conf}"
    test_name="$(basename "$base")"
    expected="${base}.expected"
    actual="${base}.actual"

    if [[ ! -f "$expected" ]]; then
        echo "[FAIL] $test_name: missing expected output: $expected"
        failed=$((failed + 1))
        continue
    fi

    if "$HV" -v "$config" > "$actual"; then
        if diff -u "$expected" "$actual" > /dev/null; then
            echo "[PASS] $test_name"
            passed=$((passed + 1))
            rm -f "$actual"
        else
            echo "[FAIL] $test_name: output mismatch"
            diff -u "$expected" "$actual"
            failed=$((failed + 1))
        fi
    else
        status=$?
        echo "[FAIL] $test_name: program exited with status $status"
        failed=$((failed + 1))
    fi
done

echo
echo "Tests: $total"
echo "Passed: $passed"
echo "Failed: $failed"

if [[ "$failed" -ne 0 ]]; then
    exit 1
fi

exit 0
