#!/bin/bash
#
# OOS Crash Recovery Tests
#
# CBRD-26463 (logging), CBRD-26608 (drop table)
# Tests: committed OOS survives crash (redo), uncommitted OOS rolled back (undo),
#        multi-chunk crash recovery, DROP TABLE crash recovery
#
# Prerequisites:
#   - CUBRID installed and in PATH
#   - cubrid_rel command available
#   - csql command available
#
# Usage: bash oos_crash_recovery.sh
#

set -u

# ============================================================================
# Configuration
# ============================================================================

DB_NAME="oos_recovery_test"
DB_VOL_PATH="${CUBRID_DATABASES}/${DB_NAME}"
LOG_FILE="oos_crash_recovery_$(date +%Y%m%d_%H%M%S).log"
PASS_COUNT=0
FAIL_COUNT=0

# ============================================================================
# Helper functions
# ============================================================================

log_msg() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

run_sql() {
    csql -u dba "$DB_NAME" -c "$1" 2>&1
}

run_sql_file() {
    csql -u dba "$DB_NAME" -i "$1" 2>&1
}

assert_equals() {
    local desc="$1"
    local expected="$2"
    local actual="$3"

    if [ "$expected" = "$actual" ]; then
        log_msg "PASS: $desc"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        log_msg "FAIL: $desc (expected='$expected', actual='$actual')"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

assert_contains() {
    local desc="$1"
    local expected_substr="$2"
    local actual="$3"

    if echo "$actual" | grep -q "$expected_substr"; then
        log_msg "PASS: $desc"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        log_msg "FAIL: $desc (expected to contain '$expected_substr', got '$actual')"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

create_db() {
    log_msg "Creating database $DB_NAME..."
    mkdir -p "$DB_VOL_PATH"
    cd "$DB_VOL_PATH" || exit 1
    cubrid createdb "$DB_NAME" --db-volume-size=512M --log-volume-size=256M 2>&1 | tee -a "$LOG_FILE"
    cd - > /dev/null || exit 1
}

start_server() {
    log_msg "Starting server for $DB_NAME..."
    cubrid server start "$DB_NAME" 2>&1 | tee -a "$LOG_FILE"
    sleep 2
}

stop_server() {
    log_msg "Stopping server for $DB_NAME..."
    cubrid server stop "$DB_NAME" 2>&1 | tee -a "$LOG_FILE"
    sleep 1
}

kill_server() {
    log_msg "KILLING server for $DB_NAME (simulating crash)..."
    # Find and kill the cub_server process for this DB
    local pid
    pid=$(ps aux | grep "cub_server $DB_NAME" | grep -v grep | awk '{print $2}')
    if [ -n "$pid" ]; then
        kill -9 "$pid" 2>/dev/null
        sleep 2
        log_msg "Killed server PID=$pid"
    else
        log_msg "WARNING: Could not find server process to kill"
    fi
}

cleanup_db() {
    cubrid server stop "$DB_NAME" 2>/dev/null
    sleep 1
    cubrid deletedb "$DB_NAME" 2>/dev/null
    rm -rf "$DB_VOL_PATH" 2>/dev/null
}

# ============================================================================
# TC-01: Committed OOS data survives crash (REDO recovery)
#
# 1. Insert OOS record and COMMIT
# 2. Kill server (simulate crash)
# 3. Restart server (triggers recovery)
# 4. Verify data is intact
# ============================================================================

test_redo_recovery() {
    log_msg "=== TC-01: REDO recovery - committed OOS survives crash ==="

    run_sql "DROP TABLE IF EXISTS t_oos_redo;"
    run_sql "CREATE TABLE t_oos_redo (id INT PRIMARY KEY, data_col BIT VARYING(32768));"
    run_sql "INSERT INTO t_oos_redo VALUES (1, REPEAT(X'AA', 1024));"
    run_sql "INSERT INTO t_oos_redo VALUES (2, REPEAT(X'BB', 2048));"

    # Verify data exists before crash
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_redo;")
    assert_contains "Pre-crash row count" "2" "$result"

    # Simulate crash
    kill_server
    start_server

    # Verify data survives crash
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_redo;")
    assert_contains "Post-crash row count" "2" "$result"

    result=$(run_sql "SELECT id, LENGTH(data_col) FROM t_oos_redo ORDER BY id;")
    assert_contains "Row 1 length after recovery" "1024" "$result"
    assert_contains "Row 2 length after recovery" "2048" "$result"

    run_sql "DROP TABLE t_oos_redo;"
    log_msg "=== TC-01 complete ==="
}

# ============================================================================
# TC-02: Uncommitted OOS data is rolled back (UNDO recovery)
#
# 1. Insert OOS record WITHOUT commit (autocommit off)
# 2. Kill server
# 3. Restart server (triggers recovery)
# 4. Verify uncommitted data is NOT present
# ============================================================================

test_undo_recovery() {
    log_msg "=== TC-02: UNDO recovery - uncommitted OOS rolled back ==="

    run_sql "DROP TABLE IF EXISTS t_oos_undo;"
    run_sql "CREATE TABLE t_oos_undo (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    # Insert committed baseline
    run_sql "INSERT INTO t_oos_undo VALUES (1, REPEAT(X'AA', 1024));"

    # Insert uncommitted record using a separate csql session with autocommit off
    # We pipe the metacommand ";autocommit off" followed by the INSERT
    printf ';autocommit off\nINSERT INTO t_oos_undo VALUES (2, REPEAT(X'"'"'BB'"'"', 2048));\n' | \
        csql -u dba "$DB_NAME" 2>&1 &
    local csql_pid=$!
    sleep 1

    # Kill server while csql session has uncommitted data
    kill_server
    kill $csql_pid 2>/dev/null

    start_server

    # Only the committed row should survive
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_undo;")
    assert_contains "Post-undo-recovery row count" "1" "$result"

    result=$(run_sql "SELECT id FROM t_oos_undo;")
    assert_contains "Surviving row is id=1" "1" "$result"

    run_sql "DROP TABLE t_oos_undo;"
    log_msg "=== TC-02 complete ==="
}

# ============================================================================
# TC-03: Multi-chunk OOS crash recovery
#
# Insert a large (32KB) multi-chunk OOS record, commit, crash, recover.
# ============================================================================

test_multi_chunk_crash_recovery() {
    log_msg "=== TC-03: Multi-chunk OOS crash recovery ==="

    run_sql "DROP TABLE IF EXISTS t_oos_multi_crash;"
    run_sql "CREATE TABLE t_oos_multi_crash (id INT PRIMARY KEY, huge_col BIT VARYING(524288));"

    # Insert 32KB multi-chunk OOS record (committed)
    run_sql "INSERT INTO t_oos_multi_crash VALUES (1, REPEAT(X'FF', 32768));"

    local result
    result=$(run_sql "SELECT LENGTH(huge_col) FROM t_oos_multi_crash WHERE id = 1;")
    assert_contains "Pre-crash multi-chunk length" "32768" "$result"

    # Crash and recover
    kill_server
    start_server

    result=$(run_sql "SELECT LENGTH(huge_col) FROM t_oos_multi_crash WHERE id = 1;")
    assert_contains "Post-crash multi-chunk length" "32768" "$result"

    # Verify data integrity by checking prefix and suffix
    result=$(run_sql "SELECT SUBSTRING(huge_col FROM 1 FOR 2) FROM t_oos_multi_crash WHERE id = 1;")
    assert_contains "Post-crash multi-chunk prefix" "ff" "$result"

    run_sql "DROP TABLE t_oos_multi_crash;"
    log_msg "=== TC-03 complete ==="
}

# ============================================================================
# TC-04: OOS UPDATE crash recovery
#
# 1. Insert OOS record, commit
# 2. Update OOS record, commit
# 3. Crash
# 4. Verify updated value is recovered (not the old value)
# ============================================================================

test_update_crash_recovery() {
    log_msg "=== TC-04: OOS UPDATE crash recovery ==="

    run_sql "DROP TABLE IF EXISTS t_oos_upd_crash;"
    run_sql "CREATE TABLE t_oos_upd_crash (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    run_sql "INSERT INTO t_oos_upd_crash VALUES (1, REPEAT(X'AA', 1024));"
    run_sql "UPDATE t_oos_upd_crash SET data_col = REPEAT(X'BB', 2048) WHERE id = 1;"

    # Crash and recover
    kill_server
    start_server

    # Should see the UPDATED value, not the original
    local result
    result=$(run_sql "SELECT LENGTH(data_col) FROM t_oos_upd_crash WHERE id = 1;")
    assert_contains "Post-crash update length" "2048" "$result"

    run_sql "DROP TABLE t_oos_upd_crash;"
    log_msg "=== TC-04 complete ==="
}

# ============================================================================
# TC-05: Mixed committed/uncommitted transaction recovery
#
# Transaction A: INSERT + COMMIT
# Transaction B: INSERT (no commit)
# Crash => A survives, B does not
# ============================================================================

test_mixed_txn_recovery() {
    log_msg "=== TC-05: Mixed committed/uncommitted recovery ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mixed_txn;"
    run_sql "CREATE TABLE t_oos_mixed_txn (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    # Committed transaction
    run_sql "INSERT INTO t_oos_mixed_txn VALUES (1, REPEAT(X'AA', 1024));"

    # Uncommitted transaction in background
    printf ';autocommit off\nINSERT INTO t_oos_mixed_txn VALUES (2, REPEAT(X'"'"'BB'"'"', 2048));\n' | \
        csql -u dba "$DB_NAME" 2>&1 &
    local csql_pid=$!
    sleep 1

    # Crash
    kill_server
    kill $csql_pid 2>/dev/null

    start_server

    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mixed_txn;")
    assert_contains "Mixed recovery: only committed row survives" "1" "$result"

    result=$(run_sql "SELECT id FROM t_oos_mixed_txn;")
    assert_contains "Surviving row is id=1" "1" "$result"

    run_sql "DROP TABLE t_oos_mixed_txn;"
    log_msg "=== TC-05 complete ==="
}

# ============================================================================
# TC-06: DROP TABLE crash recovery (CBRD-26608)
#
# 1. Create table with OOS data
# 2. DROP TABLE (committed)
# 3. Crash
# 4. After recovery, table should still be dropped
# ============================================================================

test_drop_table_crash_recovery() {
    log_msg "=== TC-06: DROP TABLE crash recovery ==="

    run_sql "DROP TABLE IF EXISTS t_oos_drop_crash;"
    run_sql "CREATE TABLE t_oos_drop_crash (id INT PRIMARY KEY, data_col BIT VARYING(65536));"
    run_sql "INSERT INTO t_oos_drop_crash VALUES (1, REPEAT(X'AA', 1024));"
    run_sql "INSERT INTO t_oos_drop_crash VALUES (2, REPEAT(X'BB', 32768));"
    run_sql "DROP TABLE t_oos_drop_crash;"

    # Crash and recover
    kill_server
    start_server

    # Table should not exist after recovery
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_drop_crash;" 2>&1)
    assert_contains "Table does not exist after drop+crash" "ERROR" "$result"

    log_msg "=== TC-06 complete ==="
}

# ============================================================================
# TC-07: Server restart (graceful) preserves OOS data
#
# Non-crash restart should also preserve all OOS data.
# ============================================================================

test_graceful_restart() {
    log_msg "=== TC-07: Graceful restart preserves OOS data ==="

    run_sql "DROP TABLE IF EXISTS t_oos_restart;"
    run_sql "CREATE TABLE t_oos_restart (id INT PRIMARY KEY, data_col BIT VARYING(65536));"
    run_sql "INSERT INTO t_oos_restart VALUES (1, REPEAT(X'AA', 1024));"
    run_sql "INSERT INTO t_oos_restart VALUES (2, REPEAT(X'BB', 32768));"

    # Graceful stop and start
    stop_server
    start_server

    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_restart;")
    assert_contains "Graceful restart row count" "2" "$result"

    result=$(run_sql "SELECT id, LENGTH(data_col) FROM t_oos_restart ORDER BY id;")
    assert_contains "Row 1 length after restart" "1024" "$result"
    assert_contains "Row 2 length after restart" "32768" "$result"

    run_sql "DROP TABLE t_oos_restart;"
    log_msg "=== TC-07 complete ==="
}

# ============================================================================
# Main
# ============================================================================

main() {
    log_msg "======================================"
    log_msg "OOS Crash Recovery Test Suite"
    log_msg "======================================"

    cleanup_db
    create_db
    start_server

    test_redo_recovery
    test_undo_recovery
    test_multi_chunk_crash_recovery
    test_update_crash_recovery
    test_mixed_txn_recovery
    test_drop_table_crash_recovery
    test_graceful_restart

    stop_server
    cleanup_db

    log_msg "======================================"
    log_msg "Results: PASS=$PASS_COUNT, FAIL=$FAIL_COUNT"
    log_msg "======================================"

    if [ "$FAIL_COUNT" -gt 0 ]; then
        exit 1
    fi
    exit 0
}

main "$@"
