
DROP TABLE IF EXISTS sales_mon_tbl;
CREATE TABLE sales_mon_tbl (
yyyy INT,
mm INT,
sales_sum INT
);
INSERT INTO sales_mon_tbl VALUES
(2000, 1, 1000), (2000, 2, 770), (2000, 3, 630), (2000, 4, 890),
(2000, 5, 500), (2000, 6, 900), (2000, 7, 1300), (2000, 8, 1800),
(2000, 9, 2100), (2000, 10, 1300), (2000, 11, 1500), (2000, 12, 1610),
(2001, 1, 1010), (2001, 2, 700), (2001, 3, 600), (2001, 4, 900),
(2001, 5, 1200), (2001, 6, 1400), (2001, 7, 1700), (2001, 8, 1110),
(2001, 9, 970), (2001, 10, 690), (2001, 11, 710), (2001, 12, 880),
(2002, 1, 980), (2002, 2, 750), (2002, 3, 730), (2002, 4, 980),
(2002, 5, 1110), (2002, 6, 570), (2002, 7, 1630), (2002, 8, 1890),
(2002, 9, 2120), (2002, 10, 970), (2002, 11, 420), (2002, 12, 1300);

create or replace function demo_int (a int) return int as 
begin
        return a;
end;

create or replace function demo_string (a string) return string as 
begin
        return a;
end;

;set xasl_debug_dump=yes

SELECT /*+ recompile */ demo_int(yyyy), MAX(demo_int(sales_sum)), MIN(demo_int(sales_sum)), AVG(demo_int(sales_sum)), COUNT(demo_int(yyyy)) FROM sales_mon_tbl GROUP BY demo_int(yyyy);

SELECT /*+ recompile */ (yyyy), MAX(sales_sum), MIN(sales_sum), AVG(sales_sum), COUNT(yyyy) FROM sales_mon_tbl GROUP BY (yyyy);

SELECT /*+ recompile */ test_fc (yyyy), MAX(sales_sum), MIN(sales_sum), AVG(sales_sum), COUNT(yyyy) FROM sales_mon_tbl GROUP BY test_fc(yyyy);


SELECT /*+ recompile NO_HASH_AGGREGATE */ demo_int(yyyy), MAX(demo_int(sales_sum)) FROM sales_mon_tbl GROUP BY demo_int(yyyy); -- 9번
SELECT /*+ recompile NO_HASH_AGGREGATE */ (yyyy), MAX(sales_sum) FROM sales_mon_tbl GROUP BY (yyyy);


SELECT /*+ recompile NO_HASH_AGGREGATE */ ELT(1, yyyy), MAX(sales_sum) FROM sales_mon_tbl GROUP BY ELT(1, yyyy);
SELECT /*+ recompile NO_HASH_AGGREGATE */ yyyy, MAX(sales_sum) FROM sales_mon_tbl GROUP BY demo_int(yyyy);
SELECT /*+ recompile NO_HASH_AGGREGATE */ demo_int(yyyy), MAX(sales_sum) FROM sales_mon_tbl GROUP BY demo_int(yyyy);

SELECT /*+ recompile NO_HASH_AGGREGATE */ HEX(yyyy), sales_sum FROM sales_mon_tbl GROUP BY HEX(demo_int(yyyy)); -- 9번
SELECT /*+ recompile NO_HASH_AGGREGATE */ HEX(demo_int(yyyy)), sales_sum FROM sales_mon_tbl GROUP BY HEX(demo_int(yyyy)); -- 9번

SELECT /*+ recompile NO_HASH_AGGREGATE */ HEX(yyyy), sales_sum FROM sales_mon_tbl LIMIT 1;
SELECT /*+ recompile NO_HASH_AGGREGATE */ HEX(demo_int(yyyy)), sales_sum FROM sales_mon_tbl LIMIT 1; -- 9번


DROP TABLE IF EXISTS sales_tbl;
-- CREATE OR REPLACE FUNCTION test_fc(i int) RETURN int as language java name 'SpTest7.typetestint(int) return int';
-- CREATE OR REPLACE FUNCTION test_fc2(i string) RETURN string as language java name 'SpTest7.typeteststring(java.lang.String) return java.lang.String';


create or replace function test_fc (a int) return int as 
begin
        return a;
end;

create or replace function test_fc2 (a string) return string as 
begin
        return a;
end;

CREATE TABLE sales_tbl
(dept_no INT, name VARCHAR(20), sales_month INT, sales_amount INT DEFAULT 100, PRIMARY KEY (dept_no, name, sales_month));
INSERT INTO sales_tbl VALUES
(201, 'George' , 1, 450), (201, 'George' , 2, 250), (201, 'Laura'  , 1, 100), (201, 'Laura'  , 2, 500),
(301, 'Max'    , 1, 300), (301, 'Max'    , 2, 300),
(501, 'Stephan', 1, 300), (501, 'Stephan', 2, DEFAULT), (501, 'Chang'  , 1, 150),(501, 'Chang'  , 2, 150),
(501, 'Sue'    , 1, 150), (501, 'Sue'    , 2, 200);

-- with sp
SELECT test_fc(dept_no) AS a1, avg(sales_amount) AS a2
FROM sales_tbl
WHERE sales_amount > 200
GROUP BY a1 HAVING a2 > 200
ORDER BY a2;

-- without sp
SELECT (dept_no) AS a1, avg(sales_amount) AS a2
FROM sales_tbl
WHERE sales_amount > 200
GROUP BY a1 HAVING a2 > 200
ORDER BY a2;

-- ======

-- with sp
SELECT test_fc(dept_no), avg(sales_amount)
FROM sales_tbl
WHERE test_fc(sales_amount) > 200
GROUP BY test_fc(dept_no) HAVING test_fc(avg(sales_amount)) > 200
ORDER BY test_fc(avg(sales_amount));

-- without sp
SELECT (dept_no), avg(sales_amount)
FROM sales_tbl
WHERE (sales_amount) > 200
GROUP BY (dept_no) HAVING (avg(sales_amount)) > 200
ORDER BY (avg(sales_amount));

-- ======
-- with sp (problem)
SELECT /*+ recompile NO_HASH_AGGREGATE */ test_fc(dept_no), (name), avg(sales_amount)
FROM sales_tbl
WHERE sales_amount > 100
GROUP BY test_fc(dept_no), (name) WITH ROLLUP;

-- without sp (problem)
SELECT /*+ recompile NO_HASH_AGGREGATE */ dept_no, name, avg(sales_amount)
FROM sales_tbl
WHERE sales_amount > 100
GROUP BY CEIL (dept_no + 0.1), name WITH ROLLUP;

-- without sp
SELECT /*+ recompile NO_HASH_AGGREGATE */ dept_no, name, avg(sales_amount)
FROM sales_tbl
WHERE sales_amount > 100
GROUP BY dept_no, name WITH ROLLUP;



DROP FUNCTION test_fc;
DROP FUNCTION test_fc2;
DROP TABLE IF EXISTS sales_tbl;
