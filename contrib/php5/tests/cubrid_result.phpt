--TEST--
cubrid_result
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc')
?>
--FILE--
<?php

include_once("connect.inc");

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);
if (!$conn) {
    printf("[001] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
    exit(1);
}

if (!$req = cubrid_query("select * from code", $conn)) {
    printf("[002] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

$result = cubrid_result($req, 0);
var_dump($result);

$result = cubrid_result($req, 0, 1);
var_dump($result);

$result = cubrid_result($req, 5, "f_name");
var_dump($result);

$result = cubrid_result($req, 4, "code.f_name");
var_dump($result);

cubrid_result($req, 8, "f_name");

cubrid_close_request($req);
cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
string(1) "X"
string(5) "Mixed"
string(4) "Gold"
string(6) "Silver"
done!
