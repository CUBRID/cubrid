--TEST--
cubrid_next_result
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include_once("connect.inc");

$tmp = NULL;
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (!is_null($tmp = @cubrid_next_result())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (false !== ($tmp = cubrid_execute($conn, "SELECT * FROM code; SELECT * FROM unknown", CUBRID_EXEC_QUERY_ALL))) {
    printf("[002] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

$sql_stmt = "SELECT * FROM code; SELECT * FROM history WHERE host_year=2004 AND event_code=20281";
$res = cubrid_prepare($conn, $sql_stmt);
$req = cubrid_execute($res, CUBRID_EXEC_QUERY_ALL);

get_result_info($res);
print_field_info($res, 1);

cubrid_next_result($res);

get_result_info($res);
print_field_info($res, 1);

if (false !== ($tmp = @cubrid_next_result($res))) {
    printf("[003] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_free_result($res);
cubrid_close($conn);

print "done!";

function print_field_info($req_handle, $offset = 0)
{
    printf("\n------------ print_field_info --------------------\n");

    cubrid_field_seek($req_handle, $offset);

    $field = cubrid_fetch_field($req_handle, $offset);
    if (!$field) {
	return false;
    }

    printf("%-30s %s\n", "name:", $field->name);
    printf("%-30s %s\n", "table:", $field->table);
    printf("%-30s \"%s\"\n", "default value:", $field->def);
    printf("%-30s %d\n", "max length:", $field->max_length);
    printf("%-30s %d\n", "not null:", $field->not_null);
    printf("%-30s %d\n", "primary key:", $field->primary_key);
    printf("%-30s %d\n", "unique key:", $field->unique_key);
    printf("%-30s %d\n", "multiple key:", $field->multiple_key);
    printf("%-30s %d\n", "numeric:", $field->numeric);
    printf("%-30s %d\n", "blob:", $field->blob);

    return true;
}

function get_result_info($req_handle)
{
    printf("\n------------ get_result_info --------------------\n");

    $row_num = cubrid_num_rows($req_handle);
    if ($row_num < 0) {
	return false;
    }

    $col_num = cubrid_num_cols($req_handle);
    if ($col_num < 0) {
	return false;
    }

    $field_num = cubrid_num_fields($req_handle);
    if ($col_num < 0) {
	return false;
    }
    assert($field_num == $col_num);

    $column_name_list = cubrid_column_names($req_handle);
    if (!$column_name_list) {
	return false;
    }

    $column_type_list = cubrid_column_types($req_handle);
    if (!$column_type_list) {
	return false;
    }

    $column_last_name = cubrid_field_name($req_handle, $col_num - 1);
    if ($column_last_name < 0) {
	return false;
    }

    $column_last_table = cubrid_field_table($req_handle, $col_num - 1);
    if ($column_last_table < 0) {
	return false;
    }

    $column_last_type = cubrid_field_type($req_handle, $col_num - 1);
    if ($column_last_type < 0) {
	return false;
    }

    $column_last_len = cubrid_field_len($req_handle, $col_num - 1);
    if (!$column_last_len) {
	return false;
    }

    $column_1_flags = cubrid_field_flags($req_handle, 1);
    if ($column_1_flags < 0) {
	return false;
    }

    printf("%-30s %d\n", "Row count:", $row_num);
    printf("%-30s %d\n", "Column count:", $col_num);
    printf("\n");

    printf("%-30s %-30s %-15s\n", "Column Names", "Column Types", "Column Len");
    printf("------------------------------------------------------------------------------\n");
    $size = count($column_name_list);
    for($i = 0; $i < $size; $i++) {
	$column_len = cubrid_field_len($req_handle, $i);
	printf("%-30s %-30s %-15s\n", $column_name_list[$i], $column_type_list[$i], $column_len); 
    }
    printf("\n\n");

    printf("%-30s %s\n", "Last Column Name:", $column_last_name);
    printf("%-30s %s\n", "Last Column Table:", $column_last_table);
    printf("%-30s %s\n", "Last Column Type:", $column_last_type);
    printf("%-30s %d\n", "Last Column Len:", $column_last_len);
    printf("%-30s %s\n", "Second Column Flags:", $column_1_flags);

    printf("\n\n");

    return true;
}
?>
--CLEAN--
--EXPECTF--

Warning: Error: DBMS, -493, Syntax: In line 1, column 35 before END OF STATEMENT
Syntax error: unexpected 'unknown'  in %s on line %d

------------ get_result_info --------------------
Row count:                     6
Column count:                  2

Column Names                   Column Types                   Column Len     
------------------------------------------------------------------------------
s_name                         char                           1              
f_name                         varchar                        6              


Last Column Name:              f_name
Last Column Table:             code
Last Column Type:              varchar
Last Column Len:               6
Second Column Flags:           



------------ print_field_info --------------------
name:                          f_name
table:                         code
default value:                 ""
max length:                    0
not null:                      0
primary key:                   0
unique key:                    0
multiple key:                  1
numeric:                       0
blob:                          0

------------ get_result_info --------------------
Row count:                     4
Column count:                  5

Column Names                   Column Types                   Column Len     
------------------------------------------------------------------------------
event_code                     integer                        11             
athlete                        varchar                        40             
host_year                      integer                        11             
score                          varchar                        10             
unit                           varchar                        5              


Last Column Name:              unit
Last Column Table:             history
Last Column Type:              varchar
Last Column Len:               5
Second Column Flags:           not_null primary_key unique_key



------------ print_field_info --------------------
name:                          athlete
table:                         history
default value:                 ""
max length:                    0
not null:                      1
primary key:                   1
unique key:                    1
multiple key:                  0
numeric:                       0
blob:                          0
done!
