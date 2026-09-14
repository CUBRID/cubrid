--
-- OOS UPDATE and DELETE Operations Tests
--
-- CBRD-26352, CBRD-26521
-- Tests: UPDATE OOS columns, DELETE rows with OOS data, TRUNCATE + reinsert,
--        repeated updates, UPDATE that changes OOS to non-OOS and vice versa
--

-- ============================================================================
-- Setup
-- ============================================================================

DROP TABLE IF EXISTS t_oos_upd;

CREATE TABLE t_oos_upd (
    id INT PRIMARY KEY,
    label VARCHAR(50),
    oos_col BIT VARYING
);

-- ============================================================================
-- TC-01: UPDATE OOS column value
-- After UPDATE, new OOS value should be written; old OOS record is garbage-collected by vacuum.
-- ============================================================================

INSERT INTO t_oos_upd VALUES (1, 'original', REPEAT(X'AA', 1024));

SELECT id, label, LENGTH(oos_col) AS len,
       SUBSTRING(oos_col FROM 1 FOR 2) AS prefix
FROM t_oos_upd WHERE id = 1;

UPDATE t_oos_upd SET oos_col = REPEAT(X'BB', 2048), label = 'updated' WHERE id = 1;

SELECT id, label, LENGTH(oos_col) AS len,
       SUBSTRING(oos_col FROM 1 FOR 2) AS prefix
FROM t_oos_upd WHERE id = 1;

-- ============================================================================
-- TC-02: UPDATE non-OOS column in a record that has OOS columns
-- OOS column should remain unchanged.
-- ============================================================================

UPDATE t_oos_upd SET label = 'label_changed' WHERE id = 1;

SELECT id, label, LENGTH(oos_col) AS len,
       SUBSTRING(oos_col FROM 1 FOR 2) AS prefix
FROM t_oos_upd WHERE id = 1;

-- ============================================================================
-- TC-03: Repeated updates on same OOS column (10 times)
-- Each update creates a new OOS OID. Data accuracy must be maintained.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_repeat_upd;

CREATE TABLE t_oos_repeat_upd (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_repeat_upd VALUES (1, REPEAT(X'00', 1024));

-- Update sequentially (each update changes the value and size)
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'01', 1034) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'02', 1044) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'03', 1054) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'04', 1064) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'05', 1074) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'06', 1084) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'07', 1094) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'08', 1104) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'09', 1114) WHERE id = 1;
UPDATE t_oos_repeat_upd SET data_col = REPEAT(X'0A', 1124) WHERE id = 1;

-- Verify final value is the last update (0x0A, length 1124)
SELECT id, SUBSTRING(data_col FROM 1 FOR 2) AS prefix, LENGTH(data_col) AS len
FROM t_oos_repeat_upd WHERE id = 1;

DROP TABLE t_oos_repeat_upd;

-- ============================================================================
-- TC-04: UPDATE that changes size (single-chunk to multi-chunk)
-- ============================================================================

DROP TABLE IF EXISTS t_oos_size_change;

CREATE TABLE t_oos_size_change (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

-- Start with single-chunk OOS (1KB)
INSERT INTO t_oos_size_change VALUES (1, REPEAT(X'AA', 1024));

SELECT id, LENGTH(data_col) AS len FROM t_oos_size_change WHERE id = 1;

-- Update to multi-chunk OOS (32KB)
UPDATE t_oos_size_change SET data_col = REPEAT(X'BB', 32768) WHERE id = 1;

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_size_change WHERE id = 1;

-- Update back to single-chunk OOS (1KB)
UPDATE t_oos_size_change SET data_col = REPEAT(X'CC', 1024) WHERE id = 1;

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_size_change WHERE id = 1;

DROP TABLE t_oos_size_change;

-- ============================================================================
-- TC-05: DELETE rows with OOS columns
-- After DELETE, row should not be visible. OOS data cleaned by vacuum.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_del;

CREATE TABLE t_oos_del (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_del VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_del VALUES (2, REPEAT(X'BB', 2048));
INSERT INTO t_oos_del VALUES (3, REPEAT(X'CC', 4096));

SELECT COUNT(*) AS before_delete FROM t_oos_del;

DELETE FROM t_oos_del WHERE id = 2;

SELECT COUNT(*) AS after_delete FROM t_oos_del;

-- Verify remaining rows are intact
SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_del ORDER BY id;

DROP TABLE t_oos_del;

-- ============================================================================
-- TC-06: DELETE all rows then reinsert
-- ============================================================================

DROP TABLE IF EXISTS t_oos_del_all;

CREATE TABLE t_oos_del_all (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_del_all VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_del_all VALUES (2, REPEAT(X'BB', 2048));

DELETE FROM t_oos_del_all;

SELECT COUNT(*) AS after_delete_all FROM t_oos_del_all;

-- Reinsert after deleting all
INSERT INTO t_oos_del_all VALUES (10, REPEAT(X'DD', 1024));

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_del_all;

DROP TABLE t_oos_del_all;

-- ============================================================================
-- TC-07: TRUNCATE table with OOS data, then reinsert
-- ============================================================================

DROP TABLE IF EXISTS t_oos_truncate;

CREATE TABLE t_oos_truncate (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_truncate VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_truncate VALUES (2, REPEAT(X'BB', 32768));

TRUNCATE TABLE t_oos_truncate;

SELECT COUNT(*) AS after_truncate FROM t_oos_truncate;

-- Reinsert after truncate
INSERT INTO t_oos_truncate VALUES (100, REPEAT(X'EE', 2048));

SELECT id, LENGTH(data_col) AS len FROM t_oos_truncate;

DROP TABLE t_oos_truncate;

-- ============================================================================
-- TC-08: UPDATE multi-chunk OOS record (replace entire chain)
-- ============================================================================

DROP TABLE IF EXISTS t_oos_upd_multi;

CREATE TABLE t_oos_upd_multi (
    id INT PRIMARY KEY,
    huge_col BIT VARYING
);

-- Insert 32KB multi-chunk record
INSERT INTO t_oos_upd_multi VALUES (1, REPEAT(X'AA', 32768));

SELECT id, LENGTH(huge_col) AS len FROM t_oos_upd_multi WHERE id = 1;

-- Update to different 64KB multi-chunk record
UPDATE t_oos_upd_multi SET huge_col = REPEAT(X'BB', 65536) WHERE id = 1;

SELECT id, LENGTH(huge_col) AS len,
       SUBSTRING(huge_col FROM 1 FOR 2) AS prefix
FROM t_oos_upd_multi WHERE id = 1;

DROP TABLE t_oos_upd_multi;

-- ============================================================================
-- Cleanup
-- ============================================================================

DROP TABLE IF EXISTS t_oos_upd;

