--
-- test_buildvalue_agg.sql
-- Regression test for CBRD-26846: GROUP_CONCAT, MEDIAN, BIT_AND/OR/XOR,
-- JSON_ARRAYAGG, JSON_OBJECTAGG in BUILDVALUE_OPT parallel heap scan path.
--
-- How to run:
--   cubrid service start
--   cubrid createdb testdb en_US
--   csql -u dba testdb < test_buildvalue_agg.sql
--
-- Each query is run once with max_parallel_scan_threads=1 (serial, baseline)
-- and once with max_parallel_scan_threads=4 (parallel).
-- Results must be identical (element sets may differ in order for unordered aggs).
--

-- ============================================================
-- Setup
-- ============================================================
DROP TABLE IF EXISTS t_agg;
CREATE TABLE t_agg (
  id  INT,
  x   VARCHAR(20),
  xi  INT,
  k   VARCHAR(10),
  v   VARCHAR(20)
);

INSERT INTO t_agg VALUES
  (1, 'apple',      3, 'a', 'alpha'),
  (2, 'banana',     1, 'b', 'beta'),
  (3, 'cherry',     4, 'c', 'gamma'),
  (4, 'date',       1, 'a', NULL),
  (5, NULL,         5, 'b', 'delta'),
  (6, 'elderberry', 9, 'c', 'epsilon'),
  (7, 'fig',        2, 'd', 'zeta'),
  (8, 'grape',      6, 'a', 'eta');

-- ============================================================
-- Serial baseline (max_parallel_scan_threads=1)
-- ============================================================
SET SYSTEM PARAMETERS 'max_parallel_scan_threads=1';

SELECT '=== SERIAL ===' AS mode FROM db_root;

-- BIT_AND/OR/XOR
SELECT BIT_AND(xi), BIT_OR(xi), BIT_XOR(xi) FROM t_agg;

-- MEDIAN
SELECT MEDIAN(xi) FROM t_agg;

-- GROUP_CONCAT (no ORDER BY): elements may appear in any order in parallel mode
SELECT GROUP_CONCAT(x) FROM t_agg;

-- GROUP_CONCAT (ORDER BY): order must be preserved
SELECT GROUP_CONCAT(x ORDER BY x ASC SEPARATOR ',') FROM t_agg;
SELECT GROUP_CONCAT(x ORDER BY x DESC SEPARATOR '-') FROM t_agg;

-- GROUP_CONCAT DISTINCT
SELECT GROUP_CONCAT(DISTINCT k) FROM t_agg;

-- JSON_ARRAYAGG (includes NULL values)
SELECT JSON_ARRAYAGG(v) FROM t_agg;

-- JSON_OBJECTAGG (duplicate keys merged via json_merge_preserve)
SELECT JSON_OBJECTAGG(k, xi) FROM t_agg;

-- Mixed aggregates: verifies multiple functions run together in BUILDVALUE_OPT
SELECT BIT_AND(xi), BIT_OR(xi), BIT_XOR(xi), MEDIAN(xi), COUNT(*) FROM t_agg;
SELECT SUM(xi), GROUP_CONCAT(x ORDER BY x ASC SEPARATOR ','), MEDIAN(xi) FROM t_agg;

-- ============================================================
-- Parallel (max_parallel_scan_threads=4)
-- Expected: same results as serial above.
-- GROUP_CONCAT (no ORDER BY): element set identical, order may differ.
-- ============================================================
SET SYSTEM PARAMETERS 'max_parallel_scan_threads=4';

SELECT '=== PARALLEL ===' AS mode FROM db_root;

-- BIT_AND/OR/XOR
SELECT BIT_AND(xi), BIT_OR(xi), BIT_XOR(xi) FROM t_agg;

-- MEDIAN
SELECT MEDIAN(xi) FROM t_agg;

-- GROUP_CONCAT (no ORDER BY)
SELECT GROUP_CONCAT(x) FROM t_agg;

-- GROUP_CONCAT (ORDER BY)
SELECT GROUP_CONCAT(x ORDER BY x ASC SEPARATOR ',') FROM t_agg;
SELECT GROUP_CONCAT(x ORDER BY x DESC SEPARATOR '-') FROM t_agg;

-- GROUP_CONCAT DISTINCT
SELECT GROUP_CONCAT(DISTINCT k) FROM t_agg;

-- JSON_ARRAYAGG
SELECT JSON_ARRAYAGG(v) FROM t_agg;

-- JSON_OBJECTAGG
SELECT JSON_OBJECTAGG(k, xi) FROM t_agg;

-- Mixed aggregates
SELECT BIT_AND(xi), BIT_OR(xi), BIT_XOR(xi), MEDIAN(xi), COUNT(*) FROM t_agg;
SELECT SUM(xi), GROUP_CONCAT(x ORDER BY x ASC SEPARATOR ','), MEDIAN(xi) FROM t_agg;

-- ============================================================
-- Edge cases
-- ============================================================
SET SYSTEM PARAMETERS 'max_parallel_scan_threads=4';

SELECT '=== EDGE CASES ===' AS mode FROM db_root;

-- All NULLs: MEDIAN/BIT should return NULL, JSON_ARRAYAGG should return [null,null,...]
SELECT BIT_AND(xi), MEDIAN(xi) FROM t_agg WHERE id = 999;
SELECT JSON_ARRAYAGG(v) FROM t_agg WHERE v IS NULL;

-- Single row
SELECT BIT_AND(xi), BIT_OR(xi), BIT_XOR(xi), MEDIAN(xi),
       GROUP_CONCAT(x), JSON_ARRAYAGG(v), JSON_OBJECTAGG(k, v)
FROM t_agg WHERE id = 1;

-- ============================================================
-- Cleanup
-- ============================================================
DROP TABLE IF EXISTS t_agg;
