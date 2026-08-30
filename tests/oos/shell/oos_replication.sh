#!/bin/bash
#
# OOS Replication Tests
#
# CBRD-26463, CBRD-26521
# Tests: Master-to-Slave OOS INSERT/UPDATE/DELETE propagation,
#        multi-chunk replication, OOS OID value equivalence on slave
#
# Note: Slave OOS OIDs may differ from Master; only VALUE equivalence is guaranteed.
#
# Prerequisites:
#   - CUBRID HA configured
#   - Master and slave accessible
#
# Usage: bash oos_replication.sh [master_host] [slave_host]
#

set -u

# ============================================================================
# Configuration
# ============================================================================

MASTER_HOST="${1:-localhost}"
SLAVE_HOST="${2:-localhost}"
DB_NAME="oos_repl_test"
MASTER_PORT=33000
SLAVE_PORT=33001
REPL_WAIT_SEC=5
LOG_FILE="oos_replication_$(date +%Y%m%d_%H%M%S).log"
PASS_COUNT=0
FAIL_COUNT=0

# ============================================================================
# Helper functions
# ============================================================================

log_msg() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

run_sql_master() {
    csql -u dba "$DB_NAME" -c "$1" 2>&1
}

run_sql_slave() {
    csql -u dba "${DB_NAME}@${SLAVE_HOST}:${SLAVE_PORT}" -c "$1" 2>&1
}

wait_for_replication() {
    log_msg "Waiting ${REPL_WAIT_SEC}s for replication to propagate..."
    sleep "$REPL_WAIT_SEC"
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

assert_row_count_eq() {
    local desc="$1"
    local expected="$2"
    local query="$3"
    local is_slave="${4:-false}"

    local result
    if [ "$is_slave" = "true" ]; then
        result=$(run_sql_slave "$query")
    else
        result=$(run_sql_master "$query")
    fi

    assert_contains "$desc" "$expected" "$result"
}

# ============================================================================
# TC-01: INSERT replication - OOS record propagates to slave
# ============================================================================

test_insert_replication() {
    log_msg "=== TC-01: INSERT replication ==="

    run_sql_master "DROP TABLE IF EXISTS t_oos_repl_ins;"
    run_sql_master "CREATE TABLE t_oos_repl_ins (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    wait_for_replication

    # Insert on master
    run_sql_master "INSERT INTO t_oos_repl_ins VALUES (1, REPEAT(X'AA', 1024));"
    run_sql_master "INSERT INTO t_oos_repl_ins VALUES (2, REPEAT(X'BB', 2048));"

    wait_for_replication

    # Verify on slave
    assert_row_count_eq "Slave row count after INSERT" "2" \
        "SELECT COUNT(*) FROM t_oos_repl_ins;" "true"

    local slave_result
    slave_result=$(run_sql_slave "SELECT id, LENGTH(data_col) FROM t_oos_repl_ins ORDER BY id;")
    assert_contains "Slave row 1 length" "1024" "$slave_result"
    assert_contains "Slave row 2 length" "2048" "$slave_result"

    # Verify VALUE equivalence (not OID equivalence)
    local master_val slave_val
    master_val=$(run_sql_master "SELECT SUBSTRING(data_col FROM 1 FOR 4) FROM t_oos_repl_ins WHERE id = 1;")
    slave_val=$(run_sql_slave "SELECT SUBSTRING(data_col FROM 1 FOR 4) FROM t_oos_repl_ins WHERE id = 1;")
    assert_contains "Value equivalence for id=1" "$(echo "$master_val" | grep -o 'aa')" "$slave_val"

    run_sql_master "DROP TABLE t_oos_repl_ins;"
    log_msg "=== TC-01 complete ==="
}

# ============================================================================
# TC-02: UPDATE replication - OOS column update propagates to slave
# ============================================================================

test_update_replication() {
    log_msg "=== TC-02: UPDATE replication ==="

    run_sql_master "DROP TABLE IF EXISTS t_oos_repl_upd;"
    run_sql_master "CREATE TABLE t_oos_repl_upd (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    wait_for_replication

    run_sql_master "INSERT INTO t_oos_repl_upd VALUES (1, REPEAT(X'AA', 1024));"

    wait_for_replication

    # Update on master
    run_sql_master "UPDATE t_oos_repl_upd SET data_col = REPEAT(X'BB', 2048) WHERE id = 1;"

    wait_for_replication

    # Verify updated value on slave
    local slave_result
    slave_result=$(run_sql_slave "SELECT LENGTH(data_col) FROM t_oos_repl_upd WHERE id = 1;")
    assert_contains "Slave updated length" "2048" "$slave_result"

    run_sql_master "DROP TABLE t_oos_repl_upd;"
    log_msg "=== TC-02 complete ==="
}

# ============================================================================
# TC-03: DELETE replication - DELETE of OOS row propagates to slave
# ============================================================================

test_delete_replication() {
    log_msg "=== TC-03: DELETE replication ==="

    run_sql_master "DROP TABLE IF EXISTS t_oos_repl_del;"
    run_sql_master "CREATE TABLE t_oos_repl_del (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    wait_for_replication

    run_sql_master "INSERT INTO t_oos_repl_del VALUES (1, REPEAT(X'AA', 1024));"
    run_sql_master "INSERT INTO t_oos_repl_del VALUES (2, REPEAT(X'BB', 2048));"

    wait_for_replication

    # Delete on master
    run_sql_master "DELETE FROM t_oos_repl_del WHERE id = 1;"

    wait_for_replication

    # Verify on slave: only row 2 remains
    assert_row_count_eq "Slave count after DELETE" "1" \
        "SELECT COUNT(*) FROM t_oos_repl_del;" "true"

    local slave_result
    slave_result=$(run_sql_slave "SELECT id FROM t_oos_repl_del;")
    assert_contains "Slave surviving row is id=2" "2" "$slave_result"

    run_sql_master "DROP TABLE t_oos_repl_del;"
    log_msg "=== TC-03 complete ==="
}

# ============================================================================
# TC-04: Multi-chunk OOS replication
# Large values (>16KB) should replicate correctly across chunks.
# ============================================================================

test_multi_chunk_replication() {
    log_msg "=== TC-04: Multi-chunk OOS replication ==="

    run_sql_master "DROP TABLE IF EXISTS t_oos_repl_multi;"
    run_sql_master "CREATE TABLE t_oos_repl_multi (id INT PRIMARY KEY, huge_col BIT VARYING(524288));"

    wait_for_replication

    # Insert 32KB multi-chunk record on master
    run_sql_master "INSERT INTO t_oos_repl_multi VALUES (1, REPEAT(X'FF', 32768));"

    wait_for_replication

    # Verify on slave
    local slave_result
    slave_result=$(run_sql_slave "SELECT LENGTH(huge_col) FROM t_oos_repl_multi WHERE id = 1;")
    assert_contains "Slave multi-chunk length" "32768" "$slave_result"

    # Verify data integrity
    slave_result=$(run_sql_slave "SELECT SUBSTRING(huge_col FROM 1 FOR 2) FROM t_oos_repl_multi WHERE id = 1;")
    assert_contains "Slave multi-chunk prefix" "ff" "$slave_result"

    run_sql_master "DROP TABLE t_oos_repl_multi;"
    log_msg "=== TC-04 complete ==="
}

# ============================================================================
# TC-05: Bulk INSERT replication with OOS
# ============================================================================

test_bulk_replication() {
    log_msg "=== TC-05: Bulk INSERT replication ==="

    run_sql_master "DROP TABLE IF EXISTS t_oos_repl_bulk;"
    run_sql_master "CREATE TABLE t_oos_repl_bulk (id INT PRIMARY KEY AUTO_INCREMENT, data_col BIT VARYING(32768));"

    wait_for_replication

    # Insert 50 OOS records on master
    run_sql_master "INSERT INTO t_oos_repl_bulk (data_col)
        SELECT REPEAT(X'EE', 600 + (ROWNUM * 20))
        FROM db_root CONNECT BY ROWNUM <= 50;"

    wait_for_replication

    # Verify count on slave
    assert_row_count_eq "Slave bulk count" "50" \
        "SELECT COUNT(*) FROM t_oos_repl_bulk;" "true"

    # Spot-check a few rows
    local master_result slave_result
    master_result=$(run_sql_master "SELECT LENGTH(data_col) FROM t_oos_repl_bulk WHERE id = 25;")
    slave_result=$(run_sql_slave "SELECT LENGTH(data_col) FROM t_oos_repl_bulk WHERE id = 25;")

    # Extract the length values and compare
    local master_len slave_len
    master_len=$(echo "$master_result" | grep -o '[0-9]\+' | head -1)
    slave_len=$(echo "$slave_result" | grep -o '[0-9]\+' | head -1)
    assert_contains "Bulk replication row 25 length match" "$master_len" "$slave_len"

    run_sql_master "DROP TABLE t_oos_repl_bulk;"
    log_msg "=== TC-05 complete ==="
}

# ============================================================================
# TC-06: UPDATE replication with multiple OOS columns
# ============================================================================

test_multi_col_update_replication() {
    log_msg "=== TC-06: Multi-column UPDATE replication ==="

    run_sql_master "DROP TABLE IF EXISTS t_oos_repl_mcol;"
    run_sql_master "CREATE TABLE t_oos_repl_mcol (
        id INT PRIMARY KEY,
        col1 BIT VARYING(32768),
        col2 BIT VARYING(32768)
    );"

    wait_for_replication

    run_sql_master "INSERT INTO t_oos_repl_mcol VALUES (1, REPEAT(X'AA', 1024), REPEAT(X'BB', 2048));"

    wait_for_replication

    # Update only one OOS column
    run_sql_master "UPDATE t_oos_repl_mcol SET col1 = REPEAT(X'CC', 4096) WHERE id = 1;"

    wait_for_replication

    local slave_result
    slave_result=$(run_sql_slave "SELECT LENGTH(col1), LENGTH(col2) FROM t_oos_repl_mcol WHERE id = 1;")
    assert_contains "Slave col1 updated length" "4096" "$slave_result"
    assert_contains "Slave col2 unchanged length" "2048" "$slave_result"

    run_sql_master "DROP TABLE t_oos_repl_mcol;"
    log_msg "=== TC-06 complete ==="
}

# ============================================================================
# Main
# ============================================================================

main() {
    log_msg "======================================"
    log_msg "OOS Replication Test Suite"
    log_msg "Master: $MASTER_HOST, Slave: $SLAVE_HOST"
    log_msg "======================================"

    test_insert_replication
    test_update_replication
    test_delete_replication
    test_multi_chunk_replication
    test_bulk_replication
    test_multi_col_update_replication

    log_msg "======================================"
    log_msg "Results: PASS=$PASS_COUNT, FAIL=$FAIL_COUNT"
    log_msg "======================================"

    if [ "$FAIL_COUNT" -gt 0 ]; then
        exit 1
    fi
    exit 0
}

main "$@"
