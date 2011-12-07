--TEST--
cubrid_rollback
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include_once("connect.inc");

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);

@cubrid_execute($conn, "DROP TABLE IF EXISTS rollback_test");
cubrid_query('CREATE TABLE rollback_test(a int)');
cubrid_query('INSERT INTO rollback_test(a) VALUE(1)');

cubrid_close($conn);
$conn = cubrid_connect_with_url($connect_url, $user, $passwd);

cubrid_set_autocommit($conn, CUBRID_AUTOCOMMIT_FALSE);

$req = cubrid_query('SELECT * FROM rollback_test');
$res = cubrid_fetch_array($req, CUBRID_ASSOC);

var_dump($res);

cubrid_query('DROP TABLE IF EXISTS rollback_test');

cubrid_rollback($conn);

cubrid_close($conn);
$conn = cubrid_connect_with_url($connect_url, $user, $passwd);

$req = cubrid_query('SELECT * FROM rollback_test');
$res = cubrid_fetch_array($req, CUBRID_ASSOC);

var_dump($res);

cubrid_close($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
array(1) {
  ["a"]=>
  string(1) "1"
}
array(1) {
  ["a"]=>
  string(1) "1"
}
done!
