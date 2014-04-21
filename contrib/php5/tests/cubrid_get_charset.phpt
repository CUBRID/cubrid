--TEST--
cubrid_get_charset
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

$charset = cubrid_get_charset($conn);
var_dump($charset);

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
string(9) "iso8859-1"
done!
