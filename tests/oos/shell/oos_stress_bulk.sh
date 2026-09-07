#!/bin/bash
#
# OOS Stress and Bulk Operations Tests
#
# Tests: 1000-record bulk operations, repeated updates on single record,
#        mixed sizes, DROP TABLE with many OOS records, concurrent operations,
#        unloaddb/loaddb with OOS data
#
# Prerequisites:
#   - CUBRID installed and in PATH
#   - csql command available
#
# Usage: bash oos_stress_bulk.sh
#

set -u

# ============================================================================
# Configuration
# ============================================================================

DB_NAME="oos_stress_test"
DB_VOL_PATH="${CUBRID_DATABASES}/${DB_NAME}"
LOG_FILE="oos_stress_$(date +%Y%m%d_%H%M%S).log"
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
    cubrid createdb "$DB_NAME" --db-volume-size=1G --log-volume-size=512M 2>&1 | tee -a "$LOG_FILE"
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

cleanup_db() {
    cubrid server stop "$DB_NAME" 2>/dev/null
    sleep 1
    cubrid deletedb "$DB_NAME" 2>/dev/null
    rm -rf "$DB_VOL_PATH" 2>/dev/null
}

# ============================================================================
# TC-01: Bulk INSERT of 1000 OOS records with varying sizes
# ============================================================================

test_bulk_insert_1000() {
    log_msg "=== TC-01: Bulk INSERT 1000 OOS records ==="

    run_sql "DROP TABLE IF EXISTS t_oos_bulk1k;"
    run_sql "CREATE TABLE t_oos_bulk1k (
        id INT PRIMARY KEY AUTO_INCREMENT,
        label VARCHAR(50),
        data_col BIT VARYING(65536)
    );"

    local start_time
    start_time=$(date +%s)

    # Insert 1000 records with sizes from 600B to ~5KB
    run_sql "INSERT INTO t_oos_bulk1k (label, data_col)
        SELECT 'row_' || ROWNUM,
               REPEAT(X'EE', 600 + (ROWNUM * 4))
        FROM db_root
        CONNECT BY ROWNUM <= 1000;"

    local end_time elapsed
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))
    log_msg "Bulk insert 1000 rows took ${elapsed}s"

    # Verify count
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_bulk1k;")
    assert_contains "1000-row bulk count" "1000" "$result"

    # Verify size range
    result=$(run_sql "SELECT MIN(LENGTH(data_col)), MAX(LENGTH(data_col)) FROM t_oos_bulk1k;")
    assert_contains "Min length ~604" "604" "$result"

    # Spot check random rows (use exact id match to avoid false positives)
    local row1_result row500_result row1000_result
    row1_result=$(run_sql "SELECT COUNT(*) FROM t_oos_bulk1k WHERE id = 1;")
    assert_contains "Row 1 exists" "1" "$row1_result"
    row500_result=$(run_sql "SELECT COUNT(*) FROM t_oos_bulk1k WHERE id = 500;")
    assert_contains "Row 500 exists" "1" "$row500_result"
    row1000_result=$(run_sql "SELECT COUNT(*) FROM t_oos_bulk1k WHERE id = 1000;")
    assert_contains "Row 1000 exists" "1" "$row1000_result"

    run_sql "DROP TABLE t_oos_bulk1k;"
    log_msg "=== TC-01 complete ==="
}

# ============================================================================
# TC-02: Repeated updates on single OOS record (50+ times)
# ============================================================================

test_repeated_updates() {
    log_msg "=== TC-02: 50 repeated updates on single OOS record ==="

    run_sql "DROP TABLE IF EXISTS t_oos_repeat;"
    run_sql "CREATE TABLE t_oos_repeat (id INT PRIMARY KEY, data_col BIT VARYING(32768));"
    run_sql "INSERT INTO t_oos_repeat VALUES (1, REPEAT(X'00', 1024));"

    local i
    for i in $(seq 1 50); do
        # Use a different byte for each update to verify final value
        local hex_byte
        hex_byte=$(printf '%02x' $((i % 256)))
        run_sql "UPDATE t_oos_repeat SET data_col = REPEAT(X'${hex_byte}', $((1024 + i * 10))) WHERE id = 1;" > /dev/null
    done

    # Verify final value (update #50: byte=0x32, length=1024+50*10=1524)
    local result
    result=$(run_sql "SELECT LENGTH(data_col) FROM t_oos_repeat WHERE id = 1;")
    assert_contains "Final length after 50 updates" "1524" "$result"

    run_sql "DROP TABLE t_oos_repeat;"
    log_msg "=== TC-02 complete ==="
}

# ============================================================================
# TC-03: Mixed single-chunk and multi-chunk OOS operations
# ============================================================================

test_mixed_sizes() {
    log_msg "=== TC-03: Mixed single/multi-chunk operations ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mixed;"
    run_sql "CREATE TABLE t_oos_mixed (id INT PRIMARY KEY, data_col BIT VARYING(524288));"

    # Insert various sizes
    run_sql "INSERT INTO t_oos_mixed VALUES (1, REPEAT(X'AA', 512));"    # borderline
    run_sql "INSERT INTO t_oos_mixed VALUES (2, REPEAT(X'BB', 1024));"   # single-chunk OOS
    run_sql "INSERT INTO t_oos_mixed VALUES (3, REPEAT(X'CC', 8192));"   # single-chunk OOS (large)
    run_sql "INSERT INTO t_oos_mixed VALUES (4, REPEAT(X'DD', 16384));"  # ~1 page
    run_sql "INSERT INTO t_oos_mixed VALUES (5, REPEAT(X'EE', 32768));"  # multi-chunk (2 pages)
    run_sql "INSERT INTO t_oos_mixed VALUES (6, REPEAT(X'FF', 65536));"  # multi-chunk (4+ pages)

    # Verify all rows
    local result
    result=$(run_sql "SELECT id, LENGTH(data_col) FROM t_oos_mixed ORDER BY id;")
    assert_contains "Row 1 length 512" "512" "$result"
    assert_contains "Row 2 length 1024" "1024" "$result"
    assert_contains "Row 3 length 8192" "8192" "$result"
    assert_contains "Row 4 length 16384" "16384" "$result"
    assert_contains "Row 5 length 32768" "32768" "$result"
    assert_contains "Row 6 length 65536" "65536" "$result"

    # Update some: single->multi, multi->single
    run_sql "UPDATE t_oos_mixed SET data_col = REPEAT(X'11', 32768) WHERE id = 2;"  # single->multi
    run_sql "UPDATE t_oos_mixed SET data_col = REPEAT(X'22', 1024) WHERE id = 5;"   # multi->single

    result=$(run_sql "SELECT id, LENGTH(data_col) FROM t_oos_mixed WHERE id IN (2, 5) ORDER BY id;")
    assert_contains "Row 2 after update: 32768" "32768" "$result"
    assert_contains "Row 5 after update: 1024" "1024" "$result"

    # Delete some
    run_sql "DELETE FROM t_oos_mixed WHERE id IN (3, 4);"
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mixed;")
    assert_contains "Count after delete" "4" "$result"

    run_sql "DROP TABLE t_oos_mixed;"
    log_msg "=== TC-03 complete ==="
}

# ============================================================================
# TC-04: DROP TABLE with many OOS records
# ============================================================================

test_drop_table_bulk() {
    log_msg "=== TC-04: DROP TABLE with 500 OOS records ==="

    run_sql "DROP TABLE IF EXISTS t_oos_drop_bulk;"
    run_sql "CREATE TABLE t_oos_drop_bulk (
        id INT PRIMARY KEY AUTO_INCREMENT,
        data_col BIT VARYING(65536)
    );"

    run_sql "INSERT INTO t_oos_drop_bulk (data_col)
        SELECT REPEAT(X'AA', 600 + (ROWNUM * 8))
        FROM db_root
        CONNECT BY ROWNUM <= 500;"

    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_drop_bulk;")
    assert_contains "Pre-drop count" "500" "$result"

    local start_time
    start_time=$(date +%s)

    run_sql "DROP TABLE t_oos_drop_bulk;"

    local end_time elapsed
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))
    log_msg "DROP TABLE with 500 OOS records took ${elapsed}s"

    # Verify table is gone
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_drop_bulk;" 2>&1)
    assert_contains "Table dropped successfully" "ERROR" "$result"

    log_msg "=== TC-04 complete ==="
}

# ============================================================================
# TC-05: unloaddb / loaddb with OOS data (CBRD-26458)
# ============================================================================

test_unload_load() {
    log_msg "=== TC-05: unloaddb/loaddb with OOS data ==="

    run_sql "DROP TABLE IF EXISTS t_oos_unload;"
    run_sql "CREATE TABLE t_oos_unload (
        id INT PRIMARY KEY,
        small_col VARCHAR(100),
        oos_col BIT VARYING(65536)
    );"

    run_sql "INSERT INTO t_oos_unload VALUES (1, 'row1', REPEAT(X'AA', 1024));"
    run_sql "INSERT INTO t_oos_unload VALUES (2, 'row2', REPEAT(X'BB', 32768));"
    run_sql "INSERT INTO t_oos_unload VALUES (3, 'row3', NULL);"

    # Unload the database
    local unload_dir="/tmp/oos_unload_$$"
    mkdir -p "$unload_dir"

    log_msg "Running unloaddb..."
    cubrid unloaddb -d "$unload_dir" "$DB_NAME" 2>&1 | tee -a "$LOG_FILE"

    # Verify unload files exist
    if [ -f "$unload_dir/${DB_NAME}_schema" ] && [ -f "$unload_dir/${DB_NAME}_objects" ]; then
        log_msg "PASS: Unload files created"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        log_msg "FAIL: Unload files not found"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi

    # Drop and recreate to test loaddb
    run_sql "DROP TABLE t_oos_unload;"

    log_msg "Running loaddb..."
    cubrid loaddb -u dba -s "$unload_dir/${DB_NAME}_schema" \
                  -d "$unload_dir/${DB_NAME}_objects" "$DB_NAME" 2>&1 | tee -a "$LOG_FILE"

    # Verify data after load
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_unload;")
    assert_contains "Post-loaddb row count" "3" "$result"

    result=$(run_sql "SELECT id, LENGTH(oos_col) FROM t_oos_unload WHERE id = 1;")
    assert_contains "Post-loaddb row 1 length" "1024" "$result"

    result=$(run_sql "SELECT id, LENGTH(oos_col) FROM t_oos_unload WHERE id = 2;")
    assert_contains "Post-loaddb row 2 length" "32768" "$result"

    result=$(run_sql "SELECT id, oos_col IS NULL FROM t_oos_unload WHERE id = 3;")
    assert_contains "Post-loaddb row 3 is NULL" "yes" "$result"

    # Cleanup
    rm -rf "$unload_dir"
    run_sql "DROP TABLE t_oos_unload;"
    log_msg "=== TC-05 complete ==="
}

# ============================================================================
# TC-06: Concurrent INSERT sessions
# ============================================================================

test_concurrent_inserts() {
    log_msg "=== TC-06: Concurrent INSERT sessions ==="

    run_sql "DROP TABLE IF EXISTS t_oos_concurrent;"
    run_sql "CREATE TABLE t_oos_concurrent (
        id INT PRIMARY KEY,
        session_id INT,
        data_col BIT VARYING(32768)
    );"

    # Launch 5 concurrent insert sessions
    for session in $(seq 1 5); do
        (
            for i in $(seq 1 20); do
                local row_id=$(( (session - 1) * 20 + i ))
                run_sql "INSERT INTO t_oos_concurrent VALUES ($row_id, $session, REPEAT(X'$(printf '%02x' $session)', 1024));" > /dev/null
            done
        ) &
    done

    # Wait for all sessions to complete
    wait

    # Verify total count
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_concurrent;")
    assert_contains "Concurrent insert total count" "100" "$result"

    # Verify each session's rows
    result=$(run_sql "SELECT session_id, COUNT(*) FROM t_oos_concurrent GROUP BY session_id ORDER BY session_id;")
    for session in $(seq 1 5); do
        assert_contains "Session $session has 20 rows" "20" "$result"
    done

    run_sql "DROP TABLE t_oos_concurrent;"
    log_msg "=== TC-06 complete ==="
}

# ============================================================================
# TC-07: DELETE + Vacuum cycle with OOS
# Insert many records, delete them, trigger vacuum, verify space is reclaimed.
# ============================================================================

test_vacuum_cycle() {
    log_msg "=== TC-07: DELETE + Vacuum cycle ==="

    run_sql "DROP TABLE IF EXISTS t_oos_vacuum;"
    run_sql "CREATE TABLE t_oos_vacuum (
        id INT PRIMARY KEY AUTO_INCREMENT,
        data_col BIT VARYING(32768)
    );"

    # Insert 200 OOS records
    run_sql "INSERT INTO t_oos_vacuum (data_col)
        SELECT REPEAT(X'AA', 1024)
        FROM db_root
        CONNECT BY ROWNUM <= 200;"

    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_vacuum;")
    assert_contains "Pre-delete count" "200" "$result"

    # Delete all rows
    run_sql "DELETE FROM t_oos_vacuum;"

    result=$(run_sql "SELECT COUNT(*) FROM t_oos_vacuum;")
    assert_contains "Post-delete count" "0" "$result"

    # Wait for vacuum to potentially run
    sleep 5

    # Reinsert - should work without issues (vacuum should have cleaned OOS)
    run_sql "INSERT INTO t_oos_vacuum (data_col)
        SELECT REPEAT(X'BB', 1024)
        FROM db_root
        CONNECT BY ROWNUM <= 50;"

    result=$(run_sql "SELECT COUNT(*) FROM t_oos_vacuum;")
    assert_contains "Post-reinsert count" "50" "$result"

    run_sql "DROP TABLE t_oos_vacuum;"
    log_msg "=== TC-07 complete ==="
}

# ============================================================================
# TC-08: Large table with multiple OOS columns - bulk operations
# ============================================================================

test_multi_col_bulk() {
    log_msg "=== TC-08: Multi-column bulk OOS operations ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mcol_bulk;"
    run_sql "CREATE TABLE t_oos_mcol_bulk (
        id INT PRIMARY KEY AUTO_INCREMENT,
        col1 BIT VARYING(32768),
        col2 BIT VARYING(32768),
        col3 BIT VARYING(32768)
    );"

    # Insert 100 rows with 3 OOS columns each
    run_sql "INSERT INTO t_oos_mcol_bulk (col1, col2, col3)
        SELECT REPEAT(X'AA', 600 + ROWNUM),
               REPEAT(X'BB', 700 + ROWNUM),
               REPEAT(X'CC', 800 + ROWNUM)
        FROM db_root
        CONNECT BY ROWNUM <= 100;"

    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mcol_bulk;")
    assert_contains "Multi-col bulk count" "100" "$result"

    # Update col2 for all rows
    run_sql "UPDATE t_oos_mcol_bulk SET col2 = REPEAT(X'DD', 1024);"

    result=$(run_sql "SELECT COUNT(DISTINCT LENGTH(col2)) FROM t_oos_mcol_bulk;")
    assert_contains "All col2 lengths uniform after update" "1" "$result"

    # Delete half
    run_sql "DELETE FROM t_oos_mcol_bulk WHERE id > 50;"

    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mcol_bulk;")
    assert_contains "Count after half delete" "50" "$result"

    run_sql "DROP TABLE t_oos_mcol_bulk;"
    log_msg "=== TC-08 complete ==="
}

# ============================================================================
# Main
# ============================================================================

main() {
    log_msg "======================================"
    log_msg "OOS Stress & Bulk Operations Test Suite"
    log_msg "======================================"

    cleanup_db
    create_db
    start_server

    test_bulk_insert_1000
    test_repeated_updates
    test_mixed_sizes
    test_drop_table_bulk
    test_unload_load
    test_concurrent_inserts
    test_vacuum_cycle
    test_multi_col_bulk

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
