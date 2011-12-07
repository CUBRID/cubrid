--TEST--
cubrid_affected_rows
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
require_once('until.php')
?>
--FILE--
<?php
include_once('connect.inc');

$conn = cubrid_connect($host, $port, $db, $user, $passwd);

require_once('table.inc');

$sql_stmt = "INSERT INTO php_cubrid_test(d) VALUES('php-test')";
$req = cubrid_prepare($conn, $sql_stmt);

for ($i = 0; $i < 10; $i++) {
    cubrid_execute($req);
}
cubrid_commit($conn);

cubrid_execute($conn, "DELETE FROM php_cubrid_test WHERE d='php-test'", CUBRID_ASYNC);
var_dump(cubrid_affected_rows());

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
<?php
require_once("clean_table.inc");
?>
--EXPECTF--
int(10)
done!
