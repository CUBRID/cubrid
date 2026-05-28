/* Test BUILDVALUE_OPT parallel heap scan - CBRD-26846 */
/* New aggregate functions: BIT_AND/OR/XOR, MEDIAN, GROUP_CONCAT, JSON_ARRAYAGG, JSON_OBJECTAGG */
/*
 * Table layout (1,000,000 rows):
 *   id   INT  : 1 .. 1000000
 *   cola INT  : rownum % 100  => 0..99, each value appears exactly 10,000 times
 *   colb VARCHAR(10) : lpad(rownum%20,10,'0') => '0000000000'..'0000000019', 20 distinct values
 *   k    VARCHAR(5)  : lpad(rownum%8 ,5 ,'0') => '00000'..'00007', 8 distinct values
 *
 * Derived constants:
 *   SUM(cola)    = (0+1+...+99) * 10000 = 4950 * 10000 = 49,500,000
 *   BIT_AND(cola)= 0    (cola=0 present; any AND with 0 => 0)
 *   BIT_OR(cola) = 127  (bits 0-6 all covered by values 1,2,4,8,16,32,64)
 *   BIT_XOR(cola)= 0    (each value appears 10,000 times; even count => XOR cancels)
 *   MEDIAN(cola) = 49.5 (pos 500000=49, pos 500001=50; displayed as 4.950000000e+01)
 */

drop table if exists bv_new_tbl;
create table bv_new_tbl (id int, cola int, colb varchar(10), k varchar(5));
insert into bv_new_tbl select rownum,
    rownum % 100,
    lpad(to_char(rownum % 20), 10, '0'),
    lpad(to_char(rownum % 8), 5, '0')
from db_class a, db_class b, db_class c, db_class d, db_class e limit 1000000;

;trace on

/* 1. BIT_AND only */
/* Expected: 0 */
select bit_and(cola) from bv_new_tbl;

/* 2. BIT_OR only */
/* Expected: 127 */
select bit_or(cola) from bv_new_tbl;

/* 3. BIT_XOR only */
/* Expected: 0 */
select bit_xor(cola) from bv_new_tbl;

/* 4. BIT_AND, BIT_OR, BIT_XOR combined */
/* Expected: 0 | 127 | 0 */
select bit_and(cola), bit_or(cola), bit_xor(cola) from bv_new_tbl;

/* 5. BIT ops with WHERE predicate (cola > 0 excludes cola=0; 1 AND 2 = 0 so BIT_AND still 0) */
/* Expected: 0 | 127 | 0 */
select bit_and(cola), bit_or(cola), bit_xor(cola) from bv_new_tbl where cola > 0;

/* 6. MEDIAN only */
/* 1M rows; sorted positions 500000=49, 500001=50 => (49+50)/2 = 49.5 */
/* Expected: 4.950000000e+01 */
select median(cola) from bv_new_tbl;

/* 7. MEDIAN with WHERE predicate */
/* cola > 50: values 51..99, 49*10000=490000 rows                        */
/* positions 245000 and 245001 both in value-75 range (240001..250000)   */
/* Expected: 7.500000000e+01 */
select median(cola) from bv_new_tbl where cola > 50;

/* 8. GROUP_CONCAT DISTINCT with ORDER BY (20 distinct colb values) */
/* Expected: '0000000000,0000000001,0000000002,0000000003,0000000004,
              0000000005,0000000006,0000000007,0000000008,0000000009,
              0000000010,0000000011,0000000012,0000000013,0000000014,
              0000000015,0000000016,0000000017,0000000018,0000000019' */
select group_concat(distinct colb order by colb separator ',') from bv_new_tbl;

/* 9. GROUP_CONCAT DISTINCT cola ORDER BY ASC (100 distinct values 0..99) */
/* Expected: '0,1,2,3,4,5,6,7,8,9,10,...,98,99' */
select group_concat(distinct cola order by cola asc separator ',') from bv_new_tbl;

/* 10. GROUP_CONCAT DISTINCT cola ORDER BY DESC */
/* Expected: '99-98-97-...-1-0' */
select group_concat(distinct cola order by cola desc separator '-') from bv_new_tbl;

/* 11. GROUP_CONCAT DISTINCT k, no ORDER BY (8 distinct values; order not guaranteed) */
/* Expected: some permutation of '00000,00001,00002,00003,00004,00005,00006,00007' */
select group_concat(distinct k separator ',') from bv_new_tbl;

/* 12. JSON_ARRAYAGG (100 rows; colb=lpad(id%20,10,'0') for id=1..100) */
/* Expected: 100-element JSON array; first 19 elements "0000000001".."0000000019",
             20th="0000000000", pattern repeats (order follows scan order) */
select json_arrayagg(colb) from bv_new_tbl where id <= 100;

/* 13. JSON_OBJECTAGG with unique keys (to_char(id) for id=1..100; cola=id%100) */
/* Expected: {"1":1,"2":2,...,"99":99,"100":0}  (key order not guaranteed in JSON) */
select json_objectagg(to_char(id), cola) from bv_new_tbl where id <= 100;

/* 14. JSON_OBJECTAGG duplicate keys via k (8 distinct k, each appears 10 times for id=1..80) */
/* json_merge_preserve accumulates duplicate keys into arrays.                              */
/* Expected (key order not guaranteed):                                                     */
/*   "00000": [8,16,24,32,40,48,56,64,72,80]  (id % 8 = 0)                                */
/*   "00001": [1, 9,17,25,33,41,49,57,65,73]  (id % 8 = 1)                                */
/*   "00002": [2,10,18,26,34,42,50,58,66,74]  (id % 8 = 2)                                */
/*   "00003": [3,11,19,27,35,43,51,59,67,75]  (id % 8 = 3)                                */
/*   "00004": [4,12,20,28,36,44,52,60,68,76]  (id % 8 = 4)                                */
/*   "00005": [5,13,21,29,37,45,53,61,69,77]  (id % 8 = 5)                                */
/*   "00006": [6,14,22,30,38,46,54,62,70,78]  (id % 8 = 6)                                */
/*   "00007": [7,15,23,31,39,47,55,63,71,79]  (id % 8 = 7)                                */
select json_objectagg(k, cola) from bv_new_tbl where id <= 80;

/* 15. Mixed: BIT + MEDIAN + standard aggregates */
/* Expected: 1000000 | 0 | 99 | 49500000 | 0 | 127 | 0 | 4.950000000e+01 */
select count(*), min(cola), max(cola), sum(cola), bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;

/* 16. Mixed: GROUP_CONCAT DISTINCT + SUM + COUNT */
/* Expected: 1000000 | 49500000 | '0000000000,0000000001,...,0000000019' */
select count(*), sum(cola), group_concat(distinct colb order by colb separator ',') from bv_new_tbl;

/* 17. Mixed: JSON + COUNT + MIN/MAX (id<=100; min(cola)=0 at id=100, max(cola)=99 at id=99) */
/* Expected: 100 | 0 | 99 | {100-key JSON object} | [100-element JSON array] */
select count(*), min(cola), max(cola), json_objectagg(to_char(id), cola), json_arrayagg(colb) from bv_new_tbl where id <= 100;

/* 18. All NULL column test */
drop table if exists bv_null_new_tbl;
create table bv_null_new_tbl (id int, val int, s varchar(10));
insert into bv_null_new_tbl select rownum, null, null
from db_class a, db_class b, db_class c, db_class d limit 5000;
/* Expected: NULL | NULL | NULL | NULL  (all values NULL; no aggregation occurs) */
select bit_and(val), bit_or(val), bit_xor(val), median(val) from bv_null_new_tbl;
/* GROUP_CONCAT skips NULLs => NULL result                                        */
/* JSON_ARRAYAGG includes NULL as JSON null => [null, null, ...] (5000 elements)  */
/* Expected: NULL | [null,null,...,null] (5000 null elements) */
select group_concat(s separator ','), json_arrayagg(s) from bv_null_new_tbl;
drop table if exists bv_null_new_tbl;

/* 19. NO_PARALLEL_HEAP_SCAN hint - verify serial path (results must match tests 4 and 8) */
/* Expected: 0 | 127 | 0 | 4.950000000e+01 */
select /*+ NO_PARALLEL_HEAP_SCAN */ bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;
/* Expected: '0000000000,0000000001,...,0000000019' */
select /*+ NO_PARALLEL_HEAP_SCAN */ group_concat(distinct colb order by colb separator ',') from bv_new_tbl;

/* 20. Compare parallel vs serial: BIT + MEDIAN (both rows must be identical) */
/* Expected (both): 0 | 127 | 0 | 4.950000000e+01 */
select bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;

/* 21. Compare parallel vs serial: GROUP_CONCAT DISTINCT ORDER BY (deterministic) */
/* Expected (both): '0,1,2,3,4,5,...,98,99' */
select group_concat(distinct cola order by cola separator ',') from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ group_concat(distinct cola order by cola separator ',') from bv_new_tbl;

/* 22. Compare parallel vs serial: all new aggregates + standard aggregates */
/* Expected (both): 1000000 | 0 | 99 | 49500000 | 4.950000000e+01 | 0 | 127 | 0 | 4.950000000e+01 */
select count(*), min(cola), max(cola), sum(cola), avg(cola),
       bit_and(cola), bit_or(cola), bit_xor(cola), median(cola)
from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ count(*), min(cola), max(cola), sum(cola), avg(cola),
       bit_and(cola), bit_or(cola), bit_xor(cola), median(cola)
from bv_new_tbl;

/* 23. Compare parallel vs serial: GROUP_CONCAT + standard aggregates */
/* Expected (both): 1000000 | 49500000 | '0000000000,...,0000000019' | 4.950000000e+01 */
select count(*), sum(cola), group_concat(distinct colb order by colb separator ','), median(cola) from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ count(*), sum(cola), group_concat(distinct colb order by colb separator ','), median(cola) from bv_new_tbl;

/* 24. Compare parallel vs serial: JSON_OBJECTAGG (id<=80, 8 distinct keys) */
/* Both must return identical JSON (same 8 keys, same array values per key) */
select json_objectagg(k, cola) from bv_new_tbl where id <= 80;
select /*+ NO_PARALLEL_HEAP_SCAN */ json_objectagg(k, cola) from bv_new_tbl where id <= 80;

/* 25. Compare parallel vs serial: JSON_ARRAYAGG (id<=100) */
/* Both must return identical 100-element JSON array */
select json_arrayagg(colb) from bv_new_tbl where id <= 100;
select /*+ NO_PARALLEL_HEAP_SCAN */ json_arrayagg(colb) from bv_new_tbl where id <= 100;

;trace off

drop table if exists bv_new_tbl;
