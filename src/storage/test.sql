DROP TABLE t7;
CREATE TABLE t7 (id INT, vec VECTOR(3));

CREATE VECTOR INDEX vidx_test ON t7 (vec COSINE) WITH (M = 16, ef_construction = 64);

INSERT INTO t7 VALUES (1, '[1,2,3]');
INSERT INTO t7 VALUES (2, '[1,0,2]');
INSERT INTO t7 VALUES (3, '[1,1,1]');
INSERT INTO t7 VALUES (4, '[0,0,1]');
INSERT INTO t7 VALUES (5, '[1,1,0]');
INSERT INTO t7 VALUES (6, '[0,1,1]');
INSERT INTO t7 VALUES (7, '[0,1,0]');
INSERT INTO t7 VALUES (8, '[1,0,1]');
INSERT INTO t7 VALUES (9, '[0,0,1]');
INSERT INTO t7 VALUES (10, '[0,1,1]');

select /*+ recompile no_parallel_heap_scan */ id from t7 order by vec <c> '[0,1,0]' limit 5;


DROP TABLE u4;
CREATE TABLE u4 (id INT, vec VECTOR(3));

-- Euclidean 전용 인덱스
CREATE VECTOR INDEX vidx_test_e
ON u4 (vec EUCLIDEAN)
WITH (M = 16, ef_construction = 64);


INSERT INTO u4 VALUES 
-- G0 (exact match)
(1, '[0,1,0]'),

-- G1 (distance ~1)
(2, '[1,1,0]'),
(3, '[-1,1,0]'),
(4, '[0,2,0]'),
(5, '[0,0,0]'),

-- G2 (distance ~√2)
(6, '[1,2,0]'),
(7, '[-1,2,0]'),
(8, '[1,0,0]'),
(9, '[-1,0,0]'),
(10,'[0,1,1]'),
(11,'[0,1,-1]'),

-- G3 (distance ~√3 ~2.0)
(12,'[1,2,1]'),
(13,'[1,2,-1]'),
(14,'[-1,2,1]'),
(15,'[-1,2,-1]'),
(16,'[1,0,1]'),
(17,'[-1,0,1]'),
(18,'[1,0,-1]'),
(19,'[-1,0,-1]'),
(20,'[2,1,0]'),

-- G4 (distance 2~3)
(21,'[2,2,0]'),
(22,'[-2,2,0]'),
(23,'[2,0,0]'),
(24,'[-2,0,0]'),
(25,'[0,3,0]'),
(26,'[0,-1,0]'),
(27,'[0,1,2]'),
(28,'[0,1,-2]'),
(29,'[2,1,1]'),
(30,'[2,1,-1]'),
(31,'[-2,1,1]'),
(32,'[-2,1,-1]'),

-- G5 (far away >3)
(33,'[3,3,0]'),
(34,'[3,-1,0]'),
(35,'[-3,3,0]'),
(36,'[-3,-1,0]'),
(37,'[2,3,2]'),
(38,'[2,3,-2]'),
(39,'[-2,3,2]'),
(40,'[-2,3,-2]'),
(41,'[3,1,1]'),
(42,'[3,1,-1]'),
(43,'[-3,1,1]'),
(44,'[-3,1,-1]'),
(45,'[4,0,0]'),
(46,'[-4,0,0]'),
(47,'[0,4,0]'),
(48,'[0,-2,0]'),
(49,'[1,4,1]'),
(50,'[-1,4,-1]');

select /*+ recompile no_parallel_heap_scan */ id, vec <-> '[0, 1, 0]' from u4 order by vec <-> '[0,1,0]' limit 5;
