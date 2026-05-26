-- SORT_ORDER_WITH_LIMIT parallel sort test
--
-- Triggered when put_fn = qexec_ordby_put_next (ordbynum_val != NULL).
-- This happens for ORDER BY with LIMIT N / ORDERBY_NUM() predicates.
--
-- topnsort bypass condition:
--   topnsort is used when LIMIT * tuple_size <= sort_buffer_pages * IO_PAGESIZE
--   With defaults: 128 * 16KB = 2MB threshold.
--   For (id INT, val INT) = 8 bytes/row: LIMIT > 262144 bypasses topnsort.
--   We use LIMIT 300000 throughout to guarantee SORT_ORDER_WITH_LIMIT path.
--
-- Trace should show: PARALLEL ORDERBY with parallel_num > 1
--
-- Prerequisites: test_mode=yes (sets parallel_sort_page_threshold=0)
-- Run: csql -u dba demodb -i cbrd_26765_order_with_limit.sql

-- =====================================================================
-- Setup: t_px_sort (500000 rows)
-- =====================================================================

DROP TABLE IF EXISTS t_px_nulls, t_px_sort;

CREATE TABLE IF NOT EXISTS t_px_sort (
  id    INTEGER,
  val   INTEGER,
  name  VARCHAR(100),
  score DOUBLE
);

INSERT INTO t_px_sort
  SELECT ROWNUM, MOD(ROWNUM * 7, 1000),
	 CONCAT('name_', LPAD(CAST(MOD(ROWNUM*13,10000) AS VARCHAR), 6, '0')),
	 ROWNUM * 0.1
  FROM db_class a, db_class b, db_class c, db_class d
  LIMIT 500000;
;trace on

-- =====================================================================
-- TC-L01: LIMIT 300000 ASC — bypasses topnsort (8B * 300000 = 2.4MB > 2MB)
--   Expected: cnt = 300000, parallel_num > 1
-- =====================================================================
SELECT COUNT(*) AS cnt
FROM (SELECT id, val FROM t_px_sort ORDER BY val LIMIT 300000) sub;

-- =====================================================================
-- TC-L02: LIMIT = total row count — all 500000 rows, no loss
--   Expected: cnt = 500000
-- =====================================================================
SELECT COUNT(*) AS cnt
FROM (SELECT id FROM t_px_sort ORDER BY id LIMIT 500000) sub;

-- =====================================================================
-- TC-L03: ORDER BY DESC LIMIT 300000
--   Expected: cnt = 300000
-- =====================================================================
SELECT COUNT(*) AS cnt
FROM (SELECT id, val FROM t_px_sort ORDER BY val DESC LIMIT 300000) sub;

-- =====================================================================
-- TC-L04: Multi-column ORDER BY LIMIT 300000
--   Correctness: SUM must match serial reference
-- =====================================================================
SELECT SUM(val) AS sum_parallel
FROM (SELECT val FROM t_px_sort ORDER BY val ASC, score DESC LIMIT 300000) sub;

-- =====================================================================
-- TC-L05: ORDER BY VARCHAR LIMIT 300000
--   Expected: cnt = 300000, lexicographic order
-- =====================================================================
SELECT COUNT(*) AS cnt
FROM (SELECT id, name FROM t_px_sort ORDER BY name LIMIT 300000) sub;

-- =====================================================================
-- TC-L06: NULL values in sort key LIMIT 300000
--   Expected: cnt = 300000, NULLs ordered consistently
-- =====================================================================
CREATE TABLE IF NOT EXISTS t_px_nulls (
  id  INTEGER,
  val INTEGER,
  str VARCHAR(50)
);

INSERT INTO t_px_nulls
  SELECT ROWNUM,
	 CASE WHEN MOD(ROWNUM, 5) = 0 THEN NULL ELSE MOD(ROWNUM * 3, 100) END,
	 CASE WHEN MOD(ROWNUM, 7) = 0 THEN NULL ELSE CONCAT('str_', CAST(ROWNUM AS VARCHAR)) END
  FROM db_class a, db_class b, db_class c, db_class d
  LIMIT 500000;

SELECT COUNT(*) AS cnt
FROM (SELECT id, val FROM t_px_nulls ORDER BY val LIMIT 300000) sub;

-- =====================================================================
-- TC-L07: LIMIT 0 — expect empty result (edge case)
-- =====================================================================
SELECT COUNT(*) AS cnt
FROM (SELECT id FROM t_px_sort ORDER BY id LIMIT 0) sub;

-- =====================================================================
-- TC-L08: Boundary correctness — MAX of top-300000 <= MIN of the rest
-- =====================================================================
SELECT MAX(val) AS max_top300000
FROM (SELECT val FROM t_px_sort ORDER BY val LIMIT 300000) sub;

SELECT MIN(val) AS min_rest
FROM (SELECT val FROM t_px_sort ORDER BY val LIMIT 300000, 300000) sub;

-- =====================================================================
-- Cleanup
-- =====================================================================

DROP TABLE IF EXISTS t_px_nulls, t_px_sort;
