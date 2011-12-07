--TEST--
cubrid_fetch
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php

include_once("connect.inc");

$tmp = NULL;
$conn = NULL;

if (!is_null($tmp = @cubrid_fetch())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_fetch($conn))) {
    printf("[002] Expecting false, got %s/%s\n", gettype($tmp), $tmp);
}

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);
if (!$conn) {
    printf("[003] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
    exit(1);
}

if (!$req = cubrid_query("select * from code", $conn)) {
    printf("[004] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

$row = cubrid_fetch($req);
var_dump($row);

$row = cubrid_fetch($req, CUBRID_NUM);
var_dump($row);

$row = cubrid_fetch($req, CUBRID_ASSOC);
var_dump($row);

$row = cubrid_fetch($req, CUBRID_OBJECT);
var_dump($row);

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
array(4) {
  [0]=>
  string(1) "X"
  ["s_name"]=>
  string(1) "X"
  [1]=>
  string(5) "Mixed"
  ["f_name"]=>
  string(5) "Mixed"
}
array(2) {
  [0]=>
  string(1) "W"
  [1]=>
  string(5) "Woman"
}
array(2) {
  ["s_name"]=>
  string(1) "M"
  ["f_name"]=>
  string(3) "Man"
}
object(stdClass)#1 (2) {
  ["s_name"]=>
  string(1) "B"
  ["f_name"]=>
  string(6) "Bronze"
}
done!
