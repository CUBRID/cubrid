--TEST--
cubrid_query_timeout
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
require_once('until.php')
?>
--FILE--
<?php

include_once('connect.inc');

$conn = cubrid_connect_with_url("CUBRID:$host:$port:$db:::?login_timeout=5000&query_timeout=5000&disconnect_on_query_timeout=yes");

$req = cubrid_prepare($conn, "SELECT * FROM code");

$timeout = cubrid_get_query_timeout($req);
var_dump($timeout);

cubrid_set_query_timeout($req, 1000);
$timeout = cubrid_get_query_timeout($req);
var_dump($timeout);

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
<?php
require_once("clean_table.inc");
?>
--EXPECTF--
int(5000)
int(1000)
done!
