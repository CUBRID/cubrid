SELECT 
{'database', 'day_millisecond', 'day_second', 'day_minute', 'day_hour', 'distinctrow',
 'div', 'do', 'duplicate', 'hour_millisecond', 'hour_second', 'hour_minute', 'localtime',
 'localtimestamp', 'minute_millisecond', 'minute_second', 'mod', 'rollup',
 'second_millisecond', 'truncate', 'xor', 'year_month'} AS "R30_new_reversed_keyword"
INTO :reserved 
FROM db_root;

SELECT 'table' AS "object", class_name AS "name", '' AS "from_table", '' AS "from_method_or_storedproc_or_trigger"
FROM   _db_class WHERE class_name IN :reserved
UNION ALL
SELECT 'column', attr_name, class_of.class_name, ''
FROM   _db_attribute WHERE attr_name IN :reserved
UNION ALL
SELECT 'method', meth_name, class_of.class_name, ''
FROM   _db_method WHERE meth_name IN :reserved
UNION ALL
SELECT 'method_function', func_name, meth_of.class_of.class_name, meth_of.meth_name
FROM   _db_meth_sig WHERE func_name IN :reserved
UNION ALL
SELECT 'index', index_name, class_of.class_name, ''
FROM   _db_index WHERE index_name IN :reserved
UNION ALL
SELECT 'partition', pname, class_of.class_name, ''
FROM   _db_partition WHERE pname IN :reserved
UNION ALL
SELECT 'stored_proc', sp_name, '', ''
FROM   _db_stored_procedure WHERE sp_name IN :reserved
UNION ALL
SELECT 'stored_proc_arg', arg_name, '', sp_name
FROM   _db_stored_procedure_args WHERE arg_name IN :reserved
UNION ALL
SELECT 'user', name, '', ''
FROM   db_user WHERE LOWER(name) IN :reserved
UNION ALL
SELECT 'trigger', name, '', ''
FROM   db_trigger WHERE name IN :reserved
UNION ALL
SELECT 'trigger_attribute', target_attribute, '', name 
FROM   db_trigger WHERE target_attribute IN :reserved
UNION ALL
SELECT 'serial', name, '', ''
FROM   db_serial WHERE name IN :reserved;

