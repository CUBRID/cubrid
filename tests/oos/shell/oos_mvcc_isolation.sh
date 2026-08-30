#!/bin/bash
#
# OOS MVCC Isolation Tests (requires two concurrent sessions)
#
# CBRD-26352, CBRD-26463
# Tests: snapshot isolation between concurrent readers/writers with OOS data,
#        dirty read prevention, concurrent UPDATE visibility
#
# Prerequisites:
#   - CUBRID installed and in PATH
#   - Database already created and server running
#
# Usage: bash oos_mvcc_isolation.sh [dbname]
#

set -u

# ============================================================================
# Configuration
# ============================================================================

DB_NAME="${1:-testdb}"
LOG_FILE="oos_mvcc_isolation_$(date +%Y%m%d_%H%M%S).log"
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

assert_not_contains() {
    local desc="$1"
    local unexpected_substr="$2"
    local actual="$3"

    if echo "$actual" | grep -q "$unexpected_substr"; then
        log_msg "FAIL: $desc (should NOT contain '$unexpected_substr', but got '$actual')"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    else
        log_msg "PASS: $desc"
        PASS_COUNT=$((PASS_COUNT + 1))
    fi
}

# ============================================================================
# TC-01: Snapshot isolation - concurrent reader does not see uncommitted INSERT
#
# Session A: BEGIN, INSERT OOS record (do NOT commit)
# Session B: SELECT should NOT see the uncommitted row
# Session A: COMMIT
# Session B: new SELECT should now see the row
# ============================================================================

test_snapshot_isolation_insert() {
    log_msg "=== TC-01: Snapshot isolation - INSERT not visible until commit ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mvcc_ins;"
    run_sql "CREATE TABLE t_oos_mvcc_ins (id INT PRIMARY KEY, data_col BIT VARYING(32768));"

    # Insert baseline committed row
    run_sql "INSERT INTO t_oos_mvcc_ins VALUES (1, REPEAT(X'AA', 1024));"

    # Session A: insert uncommitted row in background
    local session_a_fifo="/tmp/oos_mvcc_session_a_$$"
    mkfifo "$session_a_fifo"

    (
        printf ';autocommit off\n'
        printf "INSERT INTO t_oos_mvcc_ins VALUES (2, REPEAT(X'BB', 2048));\n"
        # Wait for signal to commit
        cat "$session_a_fifo"
        printf "COMMIT;\n"
    ) | csql -u dba "$DB_NAME" 2>&1 &
    local session_a_pid=$!

    sleep 2  # Give session A time to execute INSERT

    # Session B: should see only 1 row (committed baseline)
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mvcc_ins;")
    assert_contains "Session B sees only committed rows" "1" "$result"

    # Signal session A to commit
    echo "" > "$session_a_fifo"
    sleep 2  # Give commit time to complete
    wait $session_a_pid 2>/dev/null

    # Session B: now should see 2 rows
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mvcc_ins;")
    assert_contains "After commit, 2 rows visible" "2" "$result"

    rm -f "$session_a_fifo"
    run_sql "DROP TABLE t_oos_mvcc_ins;"
    log_msg "=== TC-01 complete ==="
}

# ============================================================================
# TC-02: Snapshot isolation - concurrent reader sees pre-UPDATE snapshot
#
# Session A: BEGIN, UPDATE OOS column (do NOT commit)
# Session B: SELECT should see the ORIGINAL value, not the updated one
# ============================================================================

test_snapshot_isolation_update() {
    log_msg "=== TC-02: Snapshot isolation - UPDATE not visible until commit ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mvcc_upd;"
    run_sql "CREATE TABLE t_oos_mvcc_upd (id INT PRIMARY KEY, data_col BIT VARYING(32768));"
    run_sql "INSERT INTO t_oos_mvcc_upd VALUES (1, REPEAT(X'AA', 1024));"

    # Session A: update but don't commit
    local session_a_fifo="/tmp/oos_mvcc_session_a_upd_$$"
    mkfifo "$session_a_fifo"

    (
        printf ';autocommit off\n'
        printf "UPDATE t_oos_mvcc_upd SET data_col = REPEAT(X'BB', 2048) WHERE id = 1;\n"
        cat "$session_a_fifo"
        printf "COMMIT;\n"
    ) | csql -u dba "$DB_NAME" 2>&1 &
    local session_a_pid=$!

    sleep 2

    # Session B: should see original length (1024), not updated (2048)
    local result
    result=$(run_sql "SELECT LENGTH(data_col) FROM t_oos_mvcc_upd WHERE id = 1;")
    assert_contains "Session B sees original length" "1024" "$result"

    # Signal session A to commit
    echo "" > "$session_a_fifo"
    sleep 2
    wait $session_a_pid 2>/dev/null

    # After commit, should see updated length
    result=$(run_sql "SELECT LENGTH(data_col) FROM t_oos_mvcc_upd WHERE id = 1;")
    assert_contains "After commit, updated length visible" "2048" "$result"

    rm -f "$session_a_fifo"
    run_sql "DROP TABLE t_oos_mvcc_upd;"
    log_msg "=== TC-02 complete ==="
}

# ============================================================================
# TC-03: DELETE visibility - deleted row not visible to other sessions
# ============================================================================

test_snapshot_isolation_delete() {
    log_msg "=== TC-03: Snapshot isolation - DELETE not visible until commit ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mvcc_del;"
    run_sql "CREATE TABLE t_oos_mvcc_del (id INT PRIMARY KEY, data_col BIT VARYING(32768));"
    run_sql "INSERT INTO t_oos_mvcc_del VALUES (1, REPEAT(X'AA', 1024));"
    run_sql "INSERT INTO t_oos_mvcc_del VALUES (2, REPEAT(X'BB', 2048));"

    # Session A: delete row 1 but don't commit
    local session_a_fifo="/tmp/oos_mvcc_session_a_del_$$"
    mkfifo "$session_a_fifo"

    (
        printf ';autocommit off\n'
        printf "DELETE FROM t_oos_mvcc_del WHERE id = 1;\n"
        cat "$session_a_fifo"
        printf "COMMIT;\n"
    ) | csql -u dba "$DB_NAME" 2>&1 &
    local session_a_pid=$!

    sleep 2

    # Session B: should still see 2 rows (delete not committed)
    local result
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mvcc_del;")
    assert_contains "Session B still sees 2 rows" "2" "$result"

    # Signal session A to commit
    echo "" > "$session_a_fifo"
    sleep 2
    wait $session_a_pid 2>/dev/null

    # After commit, should see 1 row
    result=$(run_sql "SELECT COUNT(*) FROM t_oos_mvcc_del;")
    assert_contains "After commit, 1 row visible" "1" "$result"

    rm -f "$session_a_fifo"
    run_sql "DROP TABLE t_oos_mvcc_del;"
    log_msg "=== TC-03 complete ==="
}

# ============================================================================
# TC-04: Concurrent multi-row updates without data corruption
# Two sessions update different rows simultaneously.
# ============================================================================

test_concurrent_updates_different_rows() {
    log_msg "=== TC-04: Concurrent updates on different rows ==="

    run_sql "DROP TABLE IF EXISTS t_oos_mvcc_conc;"
    run_sql "CREATE TABLE t_oos_mvcc_conc (id INT PRIMARY KEY, data_col BIT VARYING(32768));"
    run_sql "INSERT INTO t_oos_mvcc_conc VALUES (1, REPEAT(X'AA', 1024));"
    run_sql "INSERT INTO t_oos_mvcc_conc VALUES (2, REPEAT(X'BB', 1024));"

    # Session A: update row 1
    printf ';autocommit off\nUPDATE t_oos_mvcc_conc SET data_col = REPEAT(X'"'"'CC'"'"', 2048) WHERE id = 1;\nCOMMIT;\n' | \
        csql -u dba "$DB_NAME" 2>&1 &
    local pid_a=$!

    # Session B: update row 2 (concurrently)
    printf ';autocommit off\nUPDATE t_oos_mvcc_conc SET data_col = REPEAT(X'"'"'DD'"'"', 4096) WHERE id = 2;\nCOMMIT;\n' | \
        csql -u dba "$DB_NAME" 2>&1 &
    local pid_b=$!

    wait $pid_a $pid_b

    # Verify both updates succeeded without corruption
    local result
    result=$(run_sql "SELECT id, LENGTH(data_col) FROM t_oos_mvcc_conc ORDER BY id;")
    assert_contains "Row 1 updated to 2048" "2048" "$result"
    assert_contains "Row 2 updated to 4096" "4096" "$result"

    run_sql "DROP TABLE t_oos_mvcc_conc;"
    log_msg "=== TC-04 complete ==="
}

# ============================================================================
# Main
# ============================================================================

main() {
    log_msg "======================================"
    log_msg "OOS MVCC Isolation Test Suite"
    log_msg "Database: $DB_NAME"
    log_msg "======================================"

    test_snapshot_isolation_insert
    test_snapshot_isolation_update
    test_snapshot_isolation_delete
    test_concurrent_updates_different_rows

    log_msg "======================================"
    log_msg "Results: PASS=$PASS_COUNT, FAIL=$FAIL_COUNT"
    log_msg "======================================"

    if [ "$FAIL_COUNT" -gt 0 ]; then
        exit 1
    fi
    exit 0
}

main "$@"
