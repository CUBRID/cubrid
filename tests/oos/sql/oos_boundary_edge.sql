--
-- OOS Boundary Conditions and Edge Cases Tests
--
-- CBRD-26352, CBRD-26547, CBRD-26565, CBRD-26488, CBRD-26608
-- Tests: exact threshold sizes, NULL handling, empty values, covered index (midxkey),
--        DROP TABLE cleanup, mixed OOS/non-OOS, various data types
--

-- ============================================================================
-- TC-01: Record size exactly at DB_PAGESIZE/8 threshold (2048 bytes for 16KB)
-- Record at exactly 2KB: should be borderline for OOS.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_threshold_record;

CREATE TABLE t_oos_threshold_record (
    id INT,
    -- Fixed columns contribute to record size
    filler CHAR(1900),
    var_col BIT VARYING
);

-- Record size ~2048B: filler(1900) + var_col(148) + overhead ~ 2KB
-- var_col < 512B => should NOT trigger OOS
INSERT INTO t_oos_threshold_record VALUES (1, REPEAT('X', 1900), REPEAT(X'AA', 148));

SELECT id, LENGTH(var_col) AS var_len FROM t_oos_threshold_record WHERE id = 1;

-- Now push var_col above 512B while keeping record > 2KB
INSERT INTO t_oos_threshold_record VALUES (2, REPEAT('X', 1900), REPEAT(X'BB', 600));

SELECT id, LENGTH(var_col) AS var_len FROM t_oos_threshold_record WHERE id = 2;

-- Both should be readable
SELECT id, SUBSTRING(var_col FROM 1 FOR 2) AS prefix
FROM t_oos_threshold_record ORDER BY id;

DROP TABLE t_oos_threshold_record;

-- ============================================================================
-- TC-02: Column size exactly at 512-byte threshold
-- ============================================================================

DROP TABLE IF EXISTS t_oos_threshold_col;

CREATE TABLE t_oos_threshold_col (
    id INT PRIMARY KEY,
    filler CHAR(1800),
    var_col BIT VARYING
);

-- Column at exactly 511 bytes: should NOT trigger OOS
INSERT INTO t_oos_threshold_col VALUES (1, REPEAT('A', 1800), REPEAT(X'AA', 511));

-- Column at exactly 512 bytes: boundary
INSERT INTO t_oos_threshold_col VALUES (2, REPEAT('A', 1800), REPEAT(X'BB', 512));

-- Column at 513 bytes: should trigger OOS
INSERT INTO t_oos_threshold_col VALUES (3, REPEAT('A', 1800), REPEAT(X'CC', 513));

SELECT id, LENGTH(var_col) AS col_len,
       DISK_SIZE(var_col) AS disk_size
FROM t_oos_threshold_col ORDER BY id;

-- Verify all values are correct
SELECT id, SUBSTRING(var_col FROM 1 FOR 2) AS prefix
FROM t_oos_threshold_col ORDER BY id;

DROP TABLE t_oos_threshold_col;

-- ============================================================================
-- TC-03: NULL values in OOS-eligible columns
-- NULL columns should not create OOS entries.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_null;

CREATE TABLE t_oos_null (
    id INT PRIMARY KEY,
    oos_col BIT VARYING
);

INSERT INTO t_oos_null VALUES (1, NULL);
INSERT INTO t_oos_null VALUES (2, REPEAT(X'AA', 1024));
INSERT INTO t_oos_null VALUES (3, NULL);

SELECT id, oos_col IS NULL AS is_null, LENGTH(oos_col) AS len
FROM t_oos_null ORDER BY id;

-- Update NULL to OOS value
UPDATE t_oos_null SET oos_col = REPEAT(X'BB', 2048) WHERE id = 1;

SELECT id, oos_col IS NULL AS is_null, LENGTH(oos_col) AS len
FROM t_oos_null WHERE id = 1;

-- Update OOS value to NULL
UPDATE t_oos_null SET oos_col = NULL WHERE id = 2;

SELECT id, oos_col IS NULL AS is_null, LENGTH(oos_col) AS len
FROM t_oos_null WHERE id = 2;

DROP TABLE t_oos_null;

-- ============================================================================
-- TC-04: Empty BIT VARYING value (zero-length)
-- ============================================================================

DROP TABLE IF EXISTS t_oos_empty;

CREATE TABLE t_oos_empty (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_empty VALUES (1, X'');
INSERT INTO t_oos_empty VALUES (2, REPEAT(X'AA', 1024));

SELECT id, LENGTH(data_col) AS len, data_col IS NULL AS is_null
FROM t_oos_empty ORDER BY id;

DROP TABLE t_oos_empty;

-- ============================================================================
-- TC-05: DROP TABLE with OOS data (oos_file_destroy)
-- CBRD-26608: Verify DROP TABLE properly cleans up OOS file.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_drop;

CREATE TABLE t_oos_drop (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

-- Insert various OOS records
INSERT INTO t_oos_drop VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_drop VALUES (2, REPEAT(X'BB', 32768));
INSERT INTO t_oos_drop VALUES (3, REPEAT(X'CC', 65536));

-- Verify data before drop
SELECT COUNT(*) AS row_count FROM t_oos_drop;

-- DROP TABLE should clean up both heap and OOS files
DROP TABLE t_oos_drop;

-- Verify table is gone
SELECT COUNT(*) FROM t_oos_drop;
-- Expected: ERROR - table does not exist

-- ============================================================================
-- TC-06: DROP TABLE and recreate with same name
-- Verify OOS file from old table does not interfere with new table.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_recreate;

CREATE TABLE t_oos_recreate (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_recreate VALUES (1, REPEAT(X'AA', 2048));

DROP TABLE t_oos_recreate;

-- Recreate with same name
CREATE TABLE t_oos_recreate (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_recreate VALUES (1, REPEAT(X'BB', 4096));

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_recreate WHERE id = 1;

DROP TABLE t_oos_recreate;

-- ============================================================================
-- TC-07: Covered index with OOS columns (midxkey - CBRD-26547)
-- Ensure OOS columns work correctly with composite indexes.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_index;

CREATE TABLE t_oos_index (
    id INT PRIMARY KEY,
    idx_col INT,
    oos_col BIT VARYING
);

CREATE INDEX idx_oos_test ON t_oos_index (idx_col);

INSERT INTO t_oos_index VALUES (1, 100, REPEAT(X'AA', 1024));
INSERT INTO t_oos_index VALUES (2, 200, REPEAT(X'BB', 2048));
INSERT INTO t_oos_index VALUES (3, 100, REPEAT(X'CC', 4096));

-- Index scan should work correctly even with OOS columns
SELECT id, idx_col, LENGTH(oos_col) AS oos_len
FROM t_oos_index
WHERE idx_col = 100
ORDER BY id;

-- Verify index scan returns correct OOS data
SELECT id, SUBSTRING(oos_col FROM 1 FOR 2) AS prefix
FROM t_oos_index
WHERE idx_col = 100
ORDER BY id;

DROP TABLE t_oos_index;

-- ============================================================================
-- TC-08: midxkey buffer overflow prevention (CBRD-26565)
-- Composite index on columns where some are OOS.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_midxkey;

CREATE TABLE t_oos_midxkey (
    id INT,
    key1 INT,
    key2 VARCHAR(100),
    oos_data BIT VARYING
);

CREATE INDEX idx_midx ON t_oos_midxkey (key1, key2);

INSERT INTO t_oos_midxkey VALUES (1, 10, 'abc', REPEAT(X'AA', 1024));
INSERT INTO t_oos_midxkey VALUES (2, 10, 'def', REPEAT(X'BB', 2048));
INSERT INTO t_oos_midxkey VALUES (3, 20, 'ghi', REPEAT(X'CC', 4096));

-- Multi-column index scan with OOS data in same record
SELECT id, key1, key2, LENGTH(oos_data) AS oos_len
FROM t_oos_midxkey
WHERE key1 = 10
ORDER BY key2;

DROP TABLE t_oos_midxkey;

-- ============================================================================
-- TC-09: MVCC header size lookup bounds (CBRD-26488)
-- Prevent buffer overflow when accessing mvcc_header_size_lookup with OOS flag.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_mvcc;

CREATE TABLE t_oos_mvcc (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

-- Insert, update (sets MVCC update flags), then read
INSERT INTO t_oos_mvcc VALUES (1, REPEAT(X'AA', 1024));

UPDATE t_oos_mvcc SET data_col = REPEAT(X'BB', 2048) WHERE id = 1;

-- The MVCC header should correctly handle HAS_OOS flag
SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_mvcc WHERE id = 1;

-- Multiple updates to exercise MVCC header transitions
UPDATE t_oos_mvcc SET data_col = REPEAT(X'CC', 4096) WHERE id = 1;
UPDATE t_oos_mvcc SET data_col = REPEAT(X'DD', 1024) WHERE id = 1;

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_mvcc WHERE id = 1;

DROP TABLE t_oos_mvcc;

-- ============================================================================
-- TC-10: Multiple variable-length column types with OOS
-- Test VARCHAR, BIT VARYING, STRING types together.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_mixed_types;

CREATE TABLE t_oos_mixed_types (
    id INT PRIMARY KEY,
    varchar_col VARCHAR(8192),
    bitvar_col BIT VARYING,
    string_col VARCHAR(32768)
);

INSERT INTO t_oos_mixed_types VALUES (
    1,
    REPEAT('A', 2048),
    REPEAT(X'BB', 1024),
    REPEAT('C', 4096)
);

SELECT id,
       LENGTH(varchar_col) AS vc_len,
       LENGTH(bitvar_col) AS bv_len,
       LENGTH(string_col) AS str_len
FROM t_oos_mixed_types WHERE id = 1;

-- Verify each column's data integrity
SELECT id,
       SUBSTRING(varchar_col FROM 1 FOR 3) AS vc_prefix,
       SUBSTRING(bitvar_col FROM 1 FOR 2) AS bv_prefix,
       SUBSTRING(string_col FROM 1 FOR 3) AS str_prefix
FROM t_oos_mixed_types WHERE id = 1;

DROP TABLE t_oos_mixed_types;

-- ============================================================================
-- TC-11: data_readval with COPY when OOS column (CBRD-26352)
-- Ensure reading OOS values with COPY mode works correctly.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_copy_read;

CREATE TABLE t_oos_copy_read (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_copy_read VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_copy_read VALUES (2, REPEAT(X'BB', 2048));

-- CTAS (CREATE TABLE AS SELECT) forces COPY read path
CREATE TABLE t_oos_copy_target AS
SELECT * FROM t_oos_copy_read;

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_copy_target ORDER BY id;

DROP TABLE t_oos_copy_target;
DROP TABLE t_oos_copy_read;

-- ============================================================================
-- TC-12: INSERT ... SELECT with OOS columns
-- ============================================================================

DROP TABLE IF EXISTS t_oos_ins_sel_src;
DROP TABLE IF EXISTS t_oos_ins_sel_dst;

CREATE TABLE t_oos_ins_sel_src (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

CREATE TABLE t_oos_ins_sel_dst (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_ins_sel_src VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_ins_sel_src VALUES (2, REPEAT(X'BB', 32768));

INSERT INTO t_oos_ins_sel_dst SELECT * FROM t_oos_ins_sel_src;

SELECT id, LENGTH(data_col) AS len,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_ins_sel_dst ORDER BY id;

DROP TABLE t_oos_ins_sel_src;
DROP TABLE t_oos_ins_sel_dst;

