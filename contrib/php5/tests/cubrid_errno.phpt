--TEST--
cubrid_errno
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include "connect.inc";

$tmp    = NULL;
$link   = NULL;

if (false !== ($tmp = @cubrid_errno())) {
    printf("[001] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if (null !== ($tmp = @cubrid_errno($link))) {
    printf("[002] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_errno($link, 'too many args'))) {
    printf("[002b] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!$conn = cubrid_connect($host, $port, $db,  $user, $passwd)) {
    printf("[003] Cannot connect to db server using host=%s, port=%d, dbname=%s, user=%s, passwd=***\n", $host, $port, $db, $user);
}
var_dump(cubrid_errno($conn));

cubrid_query('SELECT * FROM code', $conn);
var_dump(cubrid_errno($conn));

cubrid_close($conn);

var_dump(cubrid_errno($conn));

if (!$conn2 = cubrid_connect($host, $port, $db,  $user, $passwd)) {
    printf("[003] Cannot connect to db server using host=%s, port=%d, dbname=%s, user=%s, passwd=***\n", $host, $port, $db, $user);
}
var_dump(cubrid_errno($conn2));
cubrid_query('SELECT * FROM table_unknow', $conn2);

printf ("cubrid_error: %s\n", cubrid_error($conn2));
printf ("cubrid_error_code: %d\n", cubrid_error_code());
printf ("cubrid_error_msg: %s\n", cubrid_error_msg());
printf ("cubrid_error_facility: %d\n", cubrid_error_code_facility());

if ($conn = @cubrid_connect($host . '_unknown', $port, $db, $user . '_unknown', $passwd)) {
    printf("[005] Can connect to the server using host=%s, port=%d, dbname=%s, user=%s, passwd=***\n", $host . '_unknown', $port, $db, $user . '_unknown');
} else {
    $errno = cubrid_errno();
    if (!is_int($errno)) {
        printf("[006] Expecting int/any (e.g 1046, 2005) got %s/%s\n", gettype($errno), $errno);
    }
}

var_dump(cubrid_errno());

print "done!";
?>
--CLEAN--
--EXPECTF--
int(0)
int(0)

Warning: cubrid_errno(): %d is not a valid CUBRID-Connect resource in %s on line %d
bool(false)
int(0)

Warning: Error: DBMS, -493, Syntax: Unknown class "table_unknow". select * from table_unknow in %s on line %d
cubrid_error: Syntax: Unknown class "table_unknow". select * from table_unknow
cubrid_error_code: -493
cubrid_error_msg: Syntax: Unknown class "table_unknow". select * from table_unknow
cubrid_error_facility: 1
int(-493)
done!
