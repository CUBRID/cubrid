----------------------------------------------------------------------------

----------
-- SET/GET
----------
call cubmemc_set_string('s_key', 'd1', 0) on class cache_tbl;
call cubmemc_set_binary('b_key', X'6431', 0) on class cache_tbl;
call cubmemc_set_binary('null_key', NULL, 0) on class cache_tbl;
-- error
call cubmemc_get_string('99') on class cache_tbl;
-- OK
call cubmemc_get_string('s_key') on class cache_tbl;
call cubmemc_get_binary('s_key') on class cache_tbl;
call cubmemc_get_string('b_key') on class cache_tbl;
call cubmemc_get_binary('b_key') on class cache_tbl;
call cubmemc_get_string('null_key') on class cache_tbl;
call cubmemc_get_binary('null_key') on class cache_tbl;

----------
-- REPLACE
----------
-- error (NOT STORED) note that 128 is coerced to "128" by CUBRID
call cubmemc_replace_string(128, '1234567890', 0) on class cache_tbl;

-- OK
call cubmemc_replace_string('s_key', '1234567890', 0) on class cache_tbl;
call cubmemc_get_string('s_key') on class cache_tbl;
call cubmemc_get_binary('s_key') on class cache_tbl;
call cubmemc_replace_binary('s_key', X'cafebabe', 0) on class cache_tbl;
call cubmemc_get_string('s_key') on class cache_tbl;
call cubmemc_get_binary('s_key') on class cache_tbl;

call cubmemc_set_string('s_key', 'd1', 0) on class cache_tbl;
call cubmemc_set_binary('b_key', X'6431', 0) on class cache_tbl;

---------
-- APPEND
---------
-- error (NOT STORED)
call cubmemc_append_string(128, '1234567890', 0) on class cache_tbl;

call cubmemc_append_string('s_key', ' appended', 0) on class cache_tbl;
call cubmemc_append_binary('b_key', X'20617070656e646564',0) on class cache_tbl;
call cubmemc_get_string('s_key') on class cache_tbl;
call cubmemc_get_binary('s_key') on class cache_tbl;
call cubmemc_get_string('b_key') on class cache_tbl;
call cubmemc_get_binary('b_key') on class cache_tbl;

call cubmemc_set_string('s_key', 'd1', 0) on class cache_tbl;
call cubmemc_set_binary('b_key', X'6431', 0) on class cache_tbl;

----------
-- PREPEND
----------
-- error (NOT STORED)
call cubmemc_prepend_string(128, '1234567890', 0) on class cache_tbl;

call cubmemc_prepend_string('s_key', 'pended ', 0) on class cache_tbl;
call cubmemc_prepend_binary('b_key', X'70656e64656420',0) on class cache_tbl;
call cubmemc_get_string('s_key') on class cache_tbl;
call cubmemc_get_binary('s_key') on class cache_tbl;
call cubmemc_get_string('b_key') on class cache_tbl;
call cubmemc_get_binary('b_key') on class cache_tbl;

call cubmemc_set_string('s_key', 'd1', 0) on class cache_tbl;
call cubmemc_set_binary('b_key', X'6431', 0) on class cache_tbl;

----------------------
-- INCREMENT DECREMENT
----------------------
call cubmemc_set_binary('incdec_key', NULL, 0) on class cache_tbl;
call cubmemc_get_binary('incdec_key') on class cache_tbl;
-- OK
call cubmemc_get_binary('incdec_key') on class cache_tbl;
call cubmemc_get_string('incdec_key') on class cache_tbl;

call cubmemc_increment('incdec_key', 1) on class cache_tbl;
call cubmemc_increment('incdec_key', 1) on class cache_tbl;
call cubmemc_increment('incdec_key', 100) on class cache_tbl;
call cubmemc_decrement('incdec_key', 1) on class cache_tbl;
call cubmemc_decrement('incdec_key', 1) on class cache_tbl;
call cubmemc_decrement('incdec_key', 100) on class cache_tbl;

-- ERROR increment/decrement key can not be read by memcached_get 
-- (actually, memcached returns incremented/decremented value without flag)
call cubmemc_get_binary('incdec_key') on class cache_tbl;
call cubmemc_get_string('incdec_key') on class cache_tbl;

--------
-- DELEE
--------
-- error
call cubmemc_delete('invalid_key', 0) on class cache_tbl;

call cubmemc_delete('incdec_key', 0) on class cache_tbl;
call cubmemc_delete('s_key', 0) on class cache_tbl;
call cubmemc_delete('b_key', 0) on class cache_tbl;
call cubmemc_delete('null_key', 0) on class cache_tbl;

