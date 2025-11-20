DROP TABLE tbl;
CREATE TABLE tbl6 (id INT, vec VECTOR(3));

CREATE VECTOR INDEX vidx_test ON tbl6 (vec COSINE) WITH (M = 16, ef_construction = 64);

INSERT INTO tbl6 VALUES (1, '[1,2,3]');
INSERT INTO tbl6 VALUES (2, '[1,0,2]');
INSERT INTO tbl6 VALUES (3, '[1,1,1]');
INSERT INTO tbl6 VALUES (4, '[0,0,0]');
INSERT INTO tbl6 VALUES (5, '[1,1,0]');
INSERT INTO tbl6 VALUES (6, '[0,1,1]');
INSERT INTO tbl6 VALUES (7, '[0,1,0]');
INSERT INTO tbl6 VALUES (8, '[1,0,1]');
INSERT INTO tbl6 VALUES (9, '[0,0,1]');
INSERT INTO tbl6 VALUES (10, '[0,1,1]');

select /*+ recompile no_parallel_heap_scan */ id from tbl6 order by vec <c> '[0,0,0]' limit 5;