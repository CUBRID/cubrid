/* Test BUILDVALUE_OPT parallel heap scan - CBRD-26846 */
/* New aggregate functions: BIT_AND/OR/XOR, MEDIAN, GROUP_CONCAT, JSON_ARRAYAGG, JSON_OBJECTAGG */

drop table if exists bv_new_tbl;
create table bv_new_tbl (id int, cola int, colb varchar(10), k varchar(5));
insert into bv_new_tbl select rownum,
    rownum % 100,
    lpad(to_char(rownum % 20), 10, '0'),
    lpad(to_char(rownum % 8), 5, '0')
from db_class a, db_class b, db_class c, db_class d, db_class e limit 1000000;

;trace on

/* 1. BIT_AND only */
select bit_and(cola) from bv_new_tbl;

/* 2. BIT_OR only */
select bit_or(cola) from bv_new_tbl;

/* 3. BIT_XOR only */
select bit_xor(cola) from bv_new_tbl;

/* 4. BIT_AND, BIT_OR, BIT_XOR combined */
select bit_and(cola), bit_or(cola), bit_xor(cola) from bv_new_tbl;

/* 5. BIT ops with WHERE predicate */
select bit_and(cola), bit_or(cola), bit_xor(cola) from bv_new_tbl where cola > 0;

/* 6. MEDIAN only */
select median(cola) from bv_new_tbl;

/* 7. MEDIAN with WHERE predicate */
select median(cola) from bv_new_tbl where cola > 50;

/* 8. GROUP_CONCAT DISTINCT with ORDER BY (20 distinct values, bounded result) */
select group_concat(distinct colb order by colb separator ',') from bv_new_tbl;

/* 9. GROUP_CONCAT DISTINCT cola ORDER BY ASC (100 distinct values) */
select group_concat(distinct cola order by cola asc separator ',') from bv_new_tbl;

/* 10. GROUP_CONCAT DISTINCT cola ORDER BY DESC */
select group_concat(distinct cola order by cola desc separator '-') from bv_new_tbl;

/* 11. GROUP_CONCAT DISTINCT k, no ORDER BY (8 distinct values) */
select group_concat(distinct k separator ',') from bv_new_tbl;

/* 12. JSON_ARRAYAGG (100 rows, bounded result) */
select json_arrayagg(colb) from bv_new_tbl where id <= 100;

/* 13. JSON_OBJECTAGG (unique id as key, 100 rows) */
select json_objectagg(to_char(id), cola) from bv_new_tbl where id <= 100;

/* 14. JSON_OBJECTAGG duplicate keys via k (8 distinct keys, merges with json_merge_preserve) */
select json_objectagg(k, cola) from bv_new_tbl where id <= 80;

/* 15. Mixed: BIT + MEDIAN + standard aggregates */
select count(*), min(cola), max(cola), sum(cola), bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;

/* 16. Mixed: GROUP_CONCAT DISTINCT + SUM + COUNT */
select count(*), sum(cola), group_concat(distinct colb order by colb separator ',') from bv_new_tbl;

/* 17. Mixed: JSON + COUNT + MIN/MAX (small dataset for readable output) */
select count(*), min(cola), max(cola), json_objectagg(to_char(id), cola), json_arrayagg(colb) from bv_new_tbl where id <= 100;

/* 18. All NULL column test */
drop table if exists bv_null_new_tbl;
create table bv_null_new_tbl (id int, val int, s varchar(10));
insert into bv_null_new_tbl select rownum, null, null
from db_class a, db_class b, db_class c, db_class d limit 5000;
select bit_and(val), bit_or(val), bit_xor(val), median(val) from bv_null_new_tbl;
select group_concat(s separator ','), json_arrayagg(s) from bv_null_new_tbl;
drop table if exists bv_null_new_tbl;

/* 19. NO_PARALLEL_HEAP_SCAN hint - should NOT use buildvalue_opt */
select /*+ NO_PARALLEL_HEAP_SCAN */ bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ group_concat(distinct colb order by colb separator ',') from bv_new_tbl;

/* 20. Compare parallel vs serial: BIT + MEDIAN */
select bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ bit_and(cola), bit_or(cola), bit_xor(cola), median(cola) from bv_new_tbl;

/* 21. Compare parallel vs serial: GROUP_CONCAT DISTINCT */
select group_concat(distinct cola order by cola separator ',') from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ group_concat(distinct cola order by cola separator ',') from bv_new_tbl;

/* 22. Compare parallel vs serial: all new aggregates + standard aggregates mixed */
select count(*), min(cola), max(cola), sum(cola), avg(cola),
       bit_and(cola), bit_or(cola), bit_xor(cola), median(cola)
from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ count(*), min(cola), max(cola), sum(cola), avg(cola),
       bit_and(cola), bit_or(cola), bit_xor(cola), median(cola)
from bv_new_tbl;

/* 23. Compare parallel vs serial: GROUP_CONCAT + standard aggregates */
select count(*), sum(cola), group_concat(distinct colb order by colb separator ','), median(cola) from bv_new_tbl;
select /*+ NO_PARALLEL_HEAP_SCAN */ count(*), sum(cola), group_concat(distinct colb order by colb separator ','), median(cola) from bv_new_tbl;

/* 24. Compare parallel vs serial: JSON_OBJECTAGG (80 rows, 8 distinct keys) */
select json_objectagg(k, cola) from bv_new_tbl where id <= 80;
select /*+ NO_PARALLEL_HEAP_SCAN */ json_objectagg(k, cola) from bv_new_tbl where id <= 80;

/* 25. Compare parallel vs serial: JSON_ARRAYAGG (100 rows) */
select json_arrayagg(colb) from bv_new_tbl where id <= 100;
select /*+ NO_PARALLEL_HEAP_SCAN */ json_arrayagg(colb) from bv_new_tbl where id <= 100;

;trace off

drop table if exists bv_new_tbl;
