--TEST--
cubrid_connect
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc')
?>
--FILE--
<?php

include_once("connect.inc");

$tmp = NULL;
$conn = NULL;

if (!is_null($tmp = @cubrid_connect())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

$conn = cubrid_connect($host, $port, $db, $user, $passwd);
if (!$conn) {
    printf("[002] [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

$conn1 = cubrid_connect($host, $port, $db, $user, $passwd, FALSE);
$conn2 = cubrid_connect($host, $port, $db, $user, $passwd, TRUE);

if ($conn != $conn1) {
    printf("[003] The new_link parameter in cubrid_connect does not work!\n");
}

if ($conn == $conn2) {
    printf("[004] Can not make a new connection with the same parameters!");
}

cubrid_close($conn);
cubrid_close($conn2);

print "done!";
?>
--CLEAN--
--EXPECTF--
done!
