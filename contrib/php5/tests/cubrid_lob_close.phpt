--TEST--
cubrid_lob_close()
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include "connect.inc";

require('table.inc');

$tmp = NULL;

$fp = fopen('cubrid_logo.png', 'rb');

$cubrid_conn = cubrid_connect($host, $port, $db, $user, $passwd);
$cubrid_req = cubrid_prepare($cubrid_conn, "insert into php_cubrid_test (e) values (?)");
if (!$cubrid_req) {
    printf("[001] Sql preparation failed. [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

$cubrid_retval = cubrid_bind($cubrid_req, 1, $fp, "blob");
if (!$cubrid_retval) {
    printf("[002] Can't bind blob type parameter. [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

$cubrid_retval = cubrid_execute($cubrid_req);
if (!$cubrid_retval) {
    printf("[003] Blob data insertion failed. [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

$lobs = cubrid_lob_get($cubrid_conn, "select e from php_cubrid_test");

cubrid_lob_close(array(1, 2, 3, 4, 5));
cubrid_lob_close($lobs);

cubrid_commit($cubrid_conn);
cubrid_disconnect($cubrid_conn);

print "done!";
?>
--CLEAN--
<?php
require_once("clean_table.inc");
?>
--EXPECTF--

Warning: cubrid_lob_close(): supplied argument is not a valid CUBRID-Lob resource in %s on line %d
done!
