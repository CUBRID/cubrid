--
-- OOS Basic CRUD Consistency Tests
--
-- CBRD-26352, CBRD-26358, CBRD-26458
-- Tests: INSERT/SELECT with OOS columns, boundary conditions, NULL handling,
--        multiple OOS columns, bulk insert, DISK_SIZE verification
--
-- OOS triggers when:
--   1. Record size > DB_PAGESIZE/8 (~2KB for 16KB pages)
--   2. Variable column size > 512 bytes
--
-- Uses BIT VARYING for predictable, non-compressed size.

-- ============================================================================
-- Setup
-- ============================================================================

DROP TABLE IF EXISTS t_oos_crud;

CREATE TABLE t_oos_crud (
    id INT PRIMARY KEY,
    small_col VARCHAR(100),
    oos_col BIT VARYING  -- up to 4KB, large enough to trigger OOS
);

-- ============================================================================
-- TC-01: Basic INSERT and SELECT of OOS column
-- Insert a record with a large BIT VARYING column (>512B) that triggers OOS.
-- Verify data integrity on retrieval.
-- ============================================================================

-- Insert a 1024-byte BIT VARYING value (well above 512B threshold)
INSERT INTO t_oos_crud VALUES (1, 'row1', X'00' || REPEAT(X'AB', 1023));

SELECT id, small_col, DISK_SIZE(oos_col) AS oos_disk_size, LENGTH(oos_col) AS oos_length
FROM t_oos_crud WHERE id = 1;

-- Verify the value can be read back correctly
SELECT id, SUBSTRING(oos_col FROM 1 FOR 8) AS first_bytes
FROM t_oos_crud WHERE id = 1;

-- ============================================================================
-- TC-02: Non-triggering condition - record <= 2KB should NOT use OOS
-- Small record should remain entirely in heap.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_no_trigger;

CREATE TABLE t_oos_no_trigger (
    id INT PRIMARY KEY,
    small_data BIT VARYING
);

-- Insert a 200-byte value: record < 2KB, column < 512B => no OOS
INSERT INTO t_oos_no_trigger VALUES (1, REPEAT(X'CD', 200));

SELECT id, DISK_SIZE(small_data) AS disk_size, LENGTH(small_data) AS len
FROM t_oos_no_trigger WHERE id = 1;

DROP TABLE t_oos_no_trigger;

-- ============================================================================
-- TC-03: Partial OOS activation - mixed column sizes
-- Only columns exceeding 512B should be stored as OOS; others stay in heap.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_partial;

CREATE TABLE t_oos_partial (
    id INT PRIMARY KEY,
    small_var VARCHAR(100),           -- stays in heap (small)
    medium_var BIT VARYING,     -- stays in heap if < 512B
    large_var BIT VARYING      -- triggers OOS if > 512B
);

-- small_var=10B, medium_var=256B (<512B), large_var=1024B (>512B)
-- Total record > 2KB threshold with large_var
INSERT INTO t_oos_partial VALUES (
    1,
    'hello',
    REPEAT(X'11', 256),
    REPEAT(X'22', 1024)
);

SELECT id,
       DISK_SIZE(small_var) AS small_disk,
       DISK_SIZE(medium_var) AS medium_disk,
       DISK_SIZE(large_var) AS large_disk,
       LENGTH(large_var) AS large_len
FROM t_oos_partial WHERE id = 1;

-- Verify all values are correctly stored and retrieved
SELECT id, small_var,
       SUBSTRING(medium_var FROM 1 FOR 4) AS med_prefix,
       SUBSTRING(large_var FROM 1 FOR 4) AS large_prefix
FROM t_oos_partial WHERE id = 1;

DROP TABLE t_oos_partial;

-- ============================================================================
-- TC-04: Multiple OOS columns in one record
-- Both large columns should be stored as separate OOS entries.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_multi_col;

CREATE TABLE t_oos_multi_col (
    id INT PRIMARY KEY,
    oos_col1 BIT VARYING,
    oos_col2 BIT VARYING,
    oos_col3 BIT VARYING
);

INSERT INTO t_oos_multi_col VALUES (
    1,
    REPEAT(X'AA', 1024),
    REPEAT(X'BB', 2048),
    REPEAT(X'CC', 4096)
);

SELECT id,
       DISK_SIZE(oos_col1) AS col1_disk,
       DISK_SIZE(oos_col2) AS col2_disk,
       DISK_SIZE(oos_col3) AS col3_disk,
       LENGTH(oos_col1) AS col1_len,
       LENGTH(oos_col2) AS col2_len,
       LENGTH(oos_col3) AS col3_len
FROM t_oos_multi_col WHERE id = 1;

-- Verify each column has correct data
SELECT id,
       SUBSTRING(oos_col1 FROM 1 FOR 2) AS c1_prefix,
       SUBSTRING(oos_col2 FROM 1 FOR 2) AS c2_prefix,
       SUBSTRING(oos_col3 FROM 1 FOR 2) AS c3_prefix
FROM t_oos_multi_col WHERE id = 1;

DROP TABLE t_oos_multi_col;

-- ============================================================================
-- TC-05: Bulk insertion of 100+ OOS records with varying sizes
-- ============================================================================

DROP TABLE IF EXISTS t_oos_bulk;

CREATE TABLE t_oos_bulk (
    id INT PRIMARY KEY AUTO_INCREMENT,
    label VARCHAR(50),
    data_col BIT VARYING
);

-- Insert 100 records with sizes ranging from 600B to 5000B
-- All should trigger OOS (> 512B column in > 2KB record)
INSERT INTO t_oos_bulk (label, data_col)
    SELECT 'row_' || ROWNUM,
           REPEAT(X'EE', 600 + (ROWNUM * 44))
    FROM db_root
    CONNECT BY ROWNUM <= 100;

-- Verify count
SELECT COUNT(*) AS total_rows FROM t_oos_bulk;

-- Verify size range
SELECT MIN(LENGTH(data_col)) AS min_len,
       MAX(LENGTH(data_col)) AS max_len,
       AVG(LENGTH(data_col)) AS avg_len
FROM t_oos_bulk;

-- Verify data integrity for a sample of rows
SELECT id, label, LENGTH(data_col) AS data_len
FROM t_oos_bulk
WHERE id IN (1, 25, 50, 75, 100)
ORDER BY id;

DROP TABLE t_oos_bulk;

-- ============================================================================
-- TC-06: Multi-chunk OOS (value > page size, ~16KB)
-- Large values spanning multiple OOS pages should be stored and retrieved correctly.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_multi_chunk;

CREATE TABLE t_oos_multi_chunk (
    id INT PRIMARY KEY,
    huge_col BIT VARYING  -- up to 64KB
);

-- Insert a 32KB value (spans ~2 OOS pages on 16KB page size)
INSERT INTO t_oos_multi_chunk VALUES (1, REPEAT(X'FF', 32768));

-- Insert a 64KB value (spans ~4 OOS pages)
INSERT INTO t_oos_multi_chunk VALUES (2, REPEAT(X'DD', 65536));

SELECT id,
       DISK_SIZE(huge_col) AS disk_size,
       LENGTH(huge_col) AS len
FROM t_oos_multi_chunk ORDER BY id;

-- Verify data integrity for multi-chunk
SELECT id,
       SUBSTRING(huge_col FROM 1 FOR 4) AS prefix,
       SUBSTRING(huge_col FROM LENGTH(huge_col) - 3 FOR 4) AS suffix
FROM t_oos_multi_chunk ORDER BY id;

DROP TABLE t_oos_multi_chunk;

-- ============================================================================
-- TC-07: Mixed single-chunk and multi-chunk OOS in same table
-- ============================================================================

DROP TABLE IF EXISTS t_oos_mixed_chunks;

CREATE TABLE t_oos_mixed_chunks (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

-- Single-chunk OOS (1KB)
INSERT INTO t_oos_mixed_chunks VALUES (1, REPEAT(X'AA', 1024));

-- Multi-chunk OOS (32KB)
INSERT INTO t_oos_mixed_chunks VALUES (2, REPEAT(X'BB', 32768));

-- Small value, might not trigger OOS
INSERT INTO t_oos_mixed_chunks VALUES (3, REPEAT(X'CC', 100));

SELECT id, LENGTH(data_col) AS len
FROM t_oos_mixed_chunks ORDER BY id;

-- Verify each row's data
SELECT id,
       SUBSTRING(data_col FROM 1 FOR 2) AS prefix
FROM t_oos_mixed_chunks ORDER BY id;

DROP TABLE t_oos_mixed_chunks;

-- ============================================================================
-- TC-08: SELECT with WHERE clause on OOS column
-- ============================================================================

DROP TABLE IF EXISTS t_oos_where;

CREATE TABLE t_oos_where (
    id INT PRIMARY KEY,
    oos_data BIT VARYING
);

INSERT INTO t_oos_where VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_where VALUES (2, REPEAT(X'BB', 1024));
INSERT INTO t_oos_where VALUES (3, REPEAT(X'AA', 1024));

-- Query filtering on OOS column value
SELECT id FROM t_oos_where
WHERE oos_data = REPEAT(X'AA', 1024)
ORDER BY id;

SELECT COUNT(*) AS match_count FROM t_oos_where
WHERE oos_data = REPEAT(X'BB', 1024);

DROP TABLE t_oos_where;

-- ============================================================================
-- Cleanup
-- ============================================================================

DROP TABLE IF EXISTS t_oos_crud;

