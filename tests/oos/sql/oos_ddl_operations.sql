--
-- OOS DDL Operations Tests
--
-- Tests: ALTER TABLE with OOS columns, DDL rollback, ADD/DROP column
--        on tables with OOS data, RENAME TABLE, CREATE TABLE AS SELECT
--

-- ============================================================================
-- TC-01: ALTER TABLE ADD COLUMN to table with existing OOS data
-- Existing OOS data should remain intact after adding a new column.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_alter;

CREATE TABLE t_oos_alter (
    id INT PRIMARY KEY,
    oos_col BIT VARYING
);

INSERT INTO t_oos_alter VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_alter VALUES (2, REPEAT(X'BB', 2048));

-- Add a new column
ALTER TABLE t_oos_alter ADD COLUMN new_col VARCHAR(100);

-- Verify existing OOS data is intact
SELECT id, LENGTH(oos_col) AS oos_len, new_col
FROM t_oos_alter ORDER BY id;

-- Insert new row with both columns
INSERT INTO t_oos_alter VALUES (3, REPEAT(X'CC', 4096), 'new_value');

SELECT id, LENGTH(oos_col) AS oos_len, new_col
FROM t_oos_alter ORDER BY id;

DROP TABLE t_oos_alter;

-- ============================================================================
-- TC-02: ALTER TABLE ADD a second OOS-eligible column
-- ============================================================================

DROP TABLE IF EXISTS t_oos_add_oos_col;

CREATE TABLE t_oos_add_oos_col (
    id INT PRIMARY KEY,
    oos_col1 BIT VARYING
);

INSERT INTO t_oos_add_oos_col VALUES (1, REPEAT(X'AA', 1024));

-- Add another OOS-eligible column
ALTER TABLE t_oos_add_oos_col ADD COLUMN oos_col2 BIT VARYING;

-- Update the new OOS column
UPDATE t_oos_add_oos_col SET oos_col2 = REPEAT(X'BB', 2048) WHERE id = 1;

SELECT id, LENGTH(oos_col1) AS col1_len, LENGTH(oos_col2) AS col2_len
FROM t_oos_add_oos_col WHERE id = 1;

-- Insert a row with both OOS columns populated
INSERT INTO t_oos_add_oos_col VALUES (2, REPEAT(X'CC', 1024), REPEAT(X'DD', 4096));

SELECT id, LENGTH(oos_col1) AS col1_len, LENGTH(oos_col2) AS col2_len
FROM t_oos_add_oos_col ORDER BY id;

DROP TABLE t_oos_add_oos_col;

-- ============================================================================
-- TC-03: ALTER TABLE DROP COLUMN that contains OOS data
-- Dropping an OOS-stored column should clean up associated OOS records.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_drop_col;

CREATE TABLE t_oos_drop_col (
    id INT PRIMARY KEY,
    keep_col VARCHAR(100),
    drop_col BIT VARYING
);

INSERT INTO t_oos_drop_col VALUES (1, 'keep_me', REPEAT(X'AA', 1024));
INSERT INTO t_oos_drop_col VALUES (2, 'keep_me_too', REPEAT(X'BB', 2048));

-- Drop the OOS column
ALTER TABLE t_oos_drop_col DROP COLUMN drop_col;

-- Verify remaining data is intact
SELECT id, keep_col FROM t_oos_drop_col ORDER BY id;

-- Verify the dropped column is gone
SELECT id, drop_col FROM t_oos_drop_col;
-- Expected: ERROR - column not found

-- Insert new rows into the modified table
INSERT INTO t_oos_drop_col VALUES (3, 'new_row');

SELECT id, keep_col FROM t_oos_drop_col ORDER BY id;

DROP TABLE t_oos_drop_col;

-- ============================================================================
-- TC-04: RENAME TABLE with OOS data
-- OOS file association should survive table rename.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_rename_old;
DROP TABLE IF EXISTS t_oos_rename_new;

CREATE TABLE t_oos_rename_old (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_rename_old VALUES (1, REPEAT(X'AA', 1024));

RENAME TABLE t_oos_rename_old AS t_oos_rename_new;

-- Verify data is accessible via new name
SELECT id, LENGTH(data_col) AS len
FROM t_oos_rename_new WHERE id = 1;

-- Insert more OOS data via new name
INSERT INTO t_oos_rename_new VALUES (2, REPEAT(X'BB', 2048));

SELECT id, LENGTH(data_col) AS len
FROM t_oos_rename_new ORDER BY id;

DROP TABLE t_oos_rename_new;

-- ============================================================================
-- TC-05: CREATE TABLE AS SELECT (CTAS) with OOS data
-- OOS values should be materialized (copied as values, not OOS OIDs).
-- ============================================================================

DROP TABLE IF EXISTS t_oos_ctas_src;
DROP TABLE IF EXISTS t_oos_ctas_dst;

CREATE TABLE t_oos_ctas_src (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_ctas_src VALUES (1, REPEAT(X'AA', 1024));
INSERT INTO t_oos_ctas_src VALUES (2, REPEAT(X'BB', 32768));

CREATE TABLE t_oos_ctas_dst AS SELECT * FROM t_oos_ctas_src;

-- Verify data integrity in destination
SELECT id, LENGTH(data_col) AS len
FROM t_oos_ctas_dst ORDER BY id;

-- Verify data is independent (drop source, dst should still work)
DROP TABLE t_oos_ctas_src;

SELECT id, LENGTH(data_col) AS len
FROM t_oos_ctas_dst ORDER BY id;

DROP TABLE t_oos_ctas_dst;

-- ============================================================================
-- TC-06: DROP TABLE then CREATE TABLE with same name (OOS file isolation)
-- The new table's OOS file should be independent of the old one.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_samename;

CREATE TABLE t_oos_samename (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_samename VALUES (1, REPEAT(X'AA', 2048));
INSERT INTO t_oos_samename VALUES (2, REPEAT(X'BB', 4096));

DROP TABLE t_oos_samename;

-- Recreate with same name but different schema
CREATE TABLE t_oos_samename (
    id INT PRIMARY KEY,
    label VARCHAR(50),
    data_col BIT VARYING
);

INSERT INTO t_oos_samename VALUES (1, 'new_table', REPEAT(X'CC', 1024));

SELECT id, label, LENGTH(data_col) AS len
FROM t_oos_samename;

DROP TABLE t_oos_samename;

-- ============================================================================
-- TC-07: Multiple sequential DROP/CREATE cycles
-- Stress test OOS file cleanup across repeated DDL operations.
-- ============================================================================

DROP TABLE IF EXISTS t_oos_cycle;

-- Cycle 1
CREATE TABLE t_oos_cycle (id INT PRIMARY KEY, data_col BIT VARYING);
INSERT INTO t_oos_cycle VALUES (1, REPEAT(X'AA', 1024));
SELECT COUNT(*) AS cycle1_count FROM t_oos_cycle;
DROP TABLE t_oos_cycle;

-- Cycle 2
CREATE TABLE t_oos_cycle (id INT PRIMARY KEY, data_col BIT VARYING);
INSERT INTO t_oos_cycle VALUES (1, REPEAT(X'BB', 2048));
INSERT INTO t_oos_cycle VALUES (2, REPEAT(X'CC', 4096));
SELECT COUNT(*) AS cycle2_count FROM t_oos_cycle;
DROP TABLE t_oos_cycle;

-- Cycle 3
CREATE TABLE t_oos_cycle (id INT PRIMARY KEY, data_col BIT VARYING);
INSERT INTO t_oos_cycle VALUES (1, REPEAT(X'DD', 32768));
SELECT COUNT(*) AS cycle3_count FROM t_oos_cycle;
DROP TABLE t_oos_cycle;

-- ============================================================================
-- TC-08: ALTER TABLE CHANGE column type (OOS-eligible to non-OOS)
-- ============================================================================

DROP TABLE IF EXISTS t_oos_change_type;

CREATE TABLE t_oos_change_type (
    id INT PRIMARY KEY,
    data_col BIT VARYING
);

INSERT INTO t_oos_change_type VALUES (1, REPEAT(X'AA', 1024));

-- Change column type to a smaller fixed type
-- This should force OOS data to be read and converted
ALTER TABLE t_oos_change_type MODIFY data_col VARCHAR(100);

-- The data will be truncated/converted - verify no crash
SELECT id, LENGTH(data_col) AS len FROM t_oos_change_type WHERE id = 1;

DROP TABLE t_oos_change_type;
