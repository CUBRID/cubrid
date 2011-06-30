--TEST--
cubrid autocommit
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

cubrid_set_autocommit($conn, CUBRID_AUTOCOMMIT_TRUE);

@cubrid_execute($conn, "DROP TABLE autocommit_test");
cubrid_query('CREATE TABLE autocommit_test(a int)');
cubrid_query('INSERT INTO autocommit_test(a) VALUE(1)');

cubrid_close($conn);
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

$req = cubrid_query('SELECT * FROM autocommit_test');
$res = cubrid_fetch_array($req, CUBRID_ASSOC);

var_dump($res);

cubrid_set_autocommit($conn, CUBRID_AUTOCOMMIT_FALSE);
cubrid_query('UPDATE autocommit_test SET a=2');

cubrid_close($conn);
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

cubrid_set_autocommit($conn, CUBRID_AUTOCOMMIT_TRUE);

$req = cubrid_query('SELECT * FROM autocommit_test');
$res = cubrid_fetch_array($req, CUBRID_ASSOC);

var_dump($res);

cubrid_query('DROP TABLE autocommit_test');

cubrid_close($conn);
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

$req = cubrid_query('SELECT * FROM autocommit_test');

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

Warning: Error: DBMS, -493, Syntax: Unknown class "autocommit_test". select * from autocommit_test in %s on line %d
done!
