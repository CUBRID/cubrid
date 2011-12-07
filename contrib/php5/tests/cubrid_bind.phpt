--TEST--
cubrid_bind
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
require_once('until.php')
?>
--FILE--
<?php
include_once('connect.inc');

$tmp = NULL;
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

@cubrid_execute($conn, 'DROP TABLE bind_test');
cubrid_execute($conn, 'CREATE TABLE bind_test(c1 string, c2 char(20), c3 int, c4 double, c5 time, c6 date, c7 datetime)');

$req = cubrid_prepare($conn, 'INSERT INTO bind_test(c1, c2, c3, c4) VALUES(?, ?, ?, ?)');

if (!is_null($tmp = @cubrid_bind())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

if (false !== ($tmp = @cubrid_bind($req, 10, 'test'))) {
    printf("[002] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_bind($req, 1, 'bind test');
cubrid_bind($req, 2, 'bind test');
cubrid_bind($req, 3, 36, 'number');
cubrid_bind($req, 4, 3.6, 'double');

cubrid_execute($req);

$req = cubrid_execute($conn, "SELECT c1, c2, c3, c4 FROM bind_test WHERE c1 = 'bind test'");
$result = cubrid_fetch_assoc($req);

var_dump($result);

if (false != ($tmp = @cubrid_prepare($conn, "INSERT INTO bind_test(c1) VALUES(:%#@)"))) {
    printf("[003] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if (false != ($tmp = @cubrid_prepare($conn, "INSERT INTO bind_test(c1) VALUES(:1adb)"))) {
    printf("[004] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if (false != ($tmp = @cubrid_prepare($conn, "INSERT INTO bind_test(c1) VALUES(:_a-b)"))) {
    printf("[005] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if (false != ($tmp = @cubrid_prepare($conn, "INSERT INTO bind_test(c1, c5, c6, c7) VALUES('bind time test', :_aa, :b3, ?)"))) {
    printf("[006] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

$req = cubrid_prepare($conn, "INSERT INTO bind_test(c1, c5, c6, c7) VALUES('bind time test', :_aa, :b3, :__)");

if (false != ($tmp = @cubrid_bind($req, ':_aaaaa', '13:15:45'))) {
    printf("[007] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_bind($req, ':_aa', '13:15:45');
cubrid_bind($req, ':b3', '2011-03-17');
cubrid_bind($req, ':__', '13:15:45 03/17/2011');

cubrid_execute($req);

$req = cubrid_execute($conn, "SELECT c5, c6, c7 FROM bind_test WHERE c1 = 'bind time test'");
$result = cubrid_fetch_assoc($req);

var_dump($result);

cubrid_close($conn);

print 'done!';
?>
--CLEAN--
<?php
require_once("clean_table.inc");
?>
--EXPECTF--
array(4) {
  ["c1"]=>
  string(9) "bind test"
  ["c2"]=>
  string(20) "bind test           "
  ["c3"]=>
  string(2) "36"
  ["c4"]=>
  string(18) "3.6000000000000001"
}
array(3) {
  ["c5"]=>
  string(8) "13:15:45"
  ["c6"]=>
  string(10) "2011-03-17"
  ["c7"]=>
  string(23) "2011-03-17 13:15:45.000"
}
done!
