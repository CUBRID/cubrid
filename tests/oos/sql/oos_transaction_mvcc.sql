--
-- OOS Transaction (ACID) and MVCC Tests
--
-- CBRD-26352, CBRD-26463
-- Tests: ROLLBACK atomicity, isolation levels, concurrent snapshot reads,
--        durability after commit, multi-chunk transactions
--
-- NOTE: This file uses csql metacommand ";autocommit off" / ";autocommit on"
-- to control transaction boundaries. These must be on their own line.
-- Run with: csql -u dba DBNAME -i oos_transaction_mvcc.sql
--

-- ============================================================================
-- TC-01: Atomicity - ROLLBACK cancels OOS INSERT
-- ============================================================================

DROP TABLE IF EXISTS t_oos_txn;

CREATE TABLE t_oos_txn (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

;autocommit off

INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 1024));

-- Verify row exists within transaction
SELECT COUNT(*) AS in_txn_count FROM t_oos_txn;

ROLLBACK;

-- After rollback, row should not exist
SELECT COUNT(*) AS after_rollback FROM t_oos_txn;

;autocommit on

-- ============================================================================
-- TC-02: Atomicity - ROLLBACK cancels OOS UPDATE
-- ============================================================================

;autocommit off

INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 1024));
COMMIT;

-- Now update in a new transaction and rollback
UPDATE t_oos_txn SET data_col = REPEAT(X'BB', 2048) WHERE id = 1;

-- Within transaction, see updated value
SELECT id, SUBSTRING(data_col FROM 1 FOR 2) AS prefix, LENGTH(data_col) AS len
FROM t_oos_txn WHERE id = 1;

ROLLBACK;

-- After rollback, original value should be restored
SELECT id, SUBSTRING(data_col FROM 1 FOR 2) AS prefix, LENGTH(data_col) AS len
FROM t_oos_txn WHERE id = 1;

;autocommit on

-- ============================================================================
-- TC-03: Durability - committed OOS data persists
-- (This test verifies within a session; crash durability tested in shell tests)
-- ============================================================================

DROP TABLE IF EXISTS t_oos_durable;

CREATE TABLE t_oos_durable (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

;autocommit off

INSERT INTO t_oos_durable VALUES (1, REPEAT(X'DD', 2048));
INSERT INTO t_oos_durable VALUES (2, REPEAT(X'EE', 4096));

COMMIT;

-- After commit, data should be visible
SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_durable ORDER BY id;

;autocommit on

DROP TABLE t_oos_durable;

-- ============================================================================
-- TC-04: Isolation - MVCC snapshot prevents dirty reads
-- NOTE: True MVCC isolation requires two concurrent sessions.
-- This test sets up the baseline; use oos_mvcc_isolation.sh for the
-- full two-session concurrent test.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_isolation;

CREATE TABLE t_oos_isolation (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_isolation VALUES (1, REPEAT(X'AA', 1024));
COMMIT;

-- Verify baseline
SELECT COUNT(*) AS baseline FROM t_oos_isolation;

-- ============================================================================
-- TC-05: Multi-chunk OOS in transaction with ROLLBACK
-- ============================================================================

DROP TABLE IF EXISTS t_oos_txn_multi;

CREATE TABLE t_oos_txn_multi (
    id INT PRIMARY KEY,
    huge_col BIT VARYING
);

;autocommit off

-- Insert a 32KB multi-chunk record
INSERT INTO t_oos_txn_multi VALUES (1, REPEAT(X'FF', 32768));

-- Verify within transaction
SELECT id, LENGTH(huge_col) AS len FROM t_oos_txn_multi WHERE id = 1;

ROLLBACK;

-- After rollback, multi-chunk OOS should be cleaned up
SELECT COUNT(*) AS after_rollback FROM t_oos_txn_multi;

;autocommit on

DROP TABLE t_oos_txn_multi;

-- ============================================================================
-- TC-06: INSERT + UPDATE + DELETE in single transaction then COMMIT
-- ============================================================================

DROP TABLE IF EXISTS t_oos_txn_combo;

CREATE TABLE t_oos_txn_combo (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

;autocommit off

INSERT INTO t_oos_txn_combo VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_txn_combo VALUES (2, REPEAT(X'BB', 2048));
INSERT INTO t_oos_txn_combo VALUES (3, REPEAT(X'CC', 4096));

-- Update row 2
UPDATE t_oos_txn_combo SET data_col = REPEAT(X'DD', 1024) WHERE id = 2;

-- Delete row 1
DELETE FROM t_oos_txn_combo WHERE id = 1;

COMMIT;

-- After commit: row 1 gone, row 2 updated, row 3 unchanged
SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_txn_combo ORDER BY id;

;autocommit on

DROP TABLE t_oos_txn_combo;

-- ============================================================================
-- TC-07: ROLLBACK after DELETE of OOS record restores visibility
-- ============================================================================

DROP TABLE IF EXISTS t_oos_txn_del_rb;

CREATE TABLE t_oos_txn_del_rb (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_txn_del_rb VALUES (1, REPEAT(X'AA', 1024));

;autocommit off

DELETE FROM t_oos_txn_del_rb WHERE id = 1;

-- Within transaction, row is deleted
SELECT COUNT(*) AS during_delete FROM t_oos_txn_del_rb;

ROLLBACK;

-- After rollback, row should be visible again with original data
SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_txn_del_rb WHERE id = 1;

;autocommit on

DROP TABLE t_oos_txn_del_rb;

-- ============================================================================
-- Cleanup
-- ============================================================================

DROP TABLE IF EXISTS t_oos_txn;
DROP TABLE IF EXISTS t_oos_isolation;
