--TEST--
cubrid_get
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc')
?>
--FILE--
<?php

include_once("connect.inc");

$conn = cubrid_connect_with_url($connect_url);
if (!$conn) {
    printf("[001] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
    exit(1);
}

@cubrid_query("DROP TABLE IF EXISTS php_cubrid_test", $conn);

cubrid_execute($conn, "CREATE TABLE php_cubrid_test (a int AUTO_INCREMENT, b set(int), c list(int), d char(30))");

cubrid_execute($conn, "INSERT INTO php_cubrid_test(a, b, c, d) VALUES (1, {1,2,3}, {11, 22, 33, 333}, 'a')");

if (!$req = cubrid_execute($conn, "select * from php_cubrid_test", CUBRID_INCLUDE_OID)) {
    printf("[002] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

$oid = cubrid_current_oid($req);
if (is_null ($oid)){
    printf("[003] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

cubrid_move_cursor($req, 1, CUBRID_CURSOR_FIRST);
$attr = cubrid_get($conn, $oid, "d");
var_dump($attr);

$attr = cubrid_get($conn, $oid, "b");
var_dump($attr);

$attr = cubrid_get($conn, $oid);
var_dump($attr);

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
string(30) "a                             "
string(9) "{1, 2, 3}"
array(4) {
  ["a"]=>
  string(1) "1"
  ["b"]=>
  array(3) {
    [0]=>
    string(1) "1"
    [1]=>
    string(1) "2"
    [2]=>
    string(1) "3"
  }
  ["c"]=>
  array(4) {
    [0]=>
    string(2) "11"
    [1]=>
    string(2) "22"
    [2]=>
    string(2) "33"
    [3]=>
    string(3) "333"
  }
  ["d"]=>
  string(30) "a                             "
}
done!
