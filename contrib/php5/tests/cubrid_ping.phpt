--TEST--
cubrid_ping()
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include_once "connect.inc";

$tmp    = NULL;
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (!is_null($tmp = @cubrid_ping($conn, $conn)))
	printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);

var_dump(cubrid_ping($conn));

// provoke an error to check if cubrid_ping resets it
$res = cubrid_query('SELECT * FROM unknown_table', $conn);
if (!($errno = cubrid_errno($conn)))
	printf("[002] Statement should have caused an error\n");

var_dump(cubrid_ping($conn));

if ($errno === cubrid_errno($conn))
	printf("[003] Error codes should have been reset\n");

var_dump(cubrid_ping());
cubrid_close($conn);

if (false !== ($tmp = cubrid_ping($conn)))
	printf("[004] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);

print "done!";
?>
--CLEAN--
--EXPECTF--
bool(true)

Warning: Error: DBMS, -493, Syntax: Unknown class "unknown_table". select * from unknown_table in %s on line %d
bool(true)
bool(true)

Warning: cubrid_ping(): %d is not a valid CUBRID-Connect resource in %s on line %d
done!
