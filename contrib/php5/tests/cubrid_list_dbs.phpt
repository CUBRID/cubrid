--TEST--
cubrid_list_dbs
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

$db_list = cubrid_list_dbs($conn);
var_dump($db_list);

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
array(1) {
  [0]=>
  string(6) "demodb"
}
done!
