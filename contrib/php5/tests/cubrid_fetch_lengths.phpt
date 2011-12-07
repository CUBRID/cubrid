--TEST--
cubrid_fetch_lengths
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

$row = cubrid_fetch_row($req);
var_dump($row);

$lens = cubrid_fetch_lengths($req);
var_dump($lens);

cubrid_close_request($req);
cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
array(2) {
  [0]=>
  string(1) "X"
  [1]=>
  string(5) "Mixed"
}
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(5)
}
done!
