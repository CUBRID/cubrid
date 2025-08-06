--TEST--
cubrid_lob_export
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

$cubrid_conn = cubrid_connect($host, $port, $db, $user, $passwd);
$cubrid_req = cubrid_prepare($cubrid_conn, "insert into php_cubrid_test (e) values (?)");
if (!$cubrid_req) {
    printf("[001] Sql preparation failed. [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

$fp = fopen('cubrid_logo.png', 'rb');

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
$ret = cubrid_lob_export($cubrid_conn, $lobs[0], "lob_test.png");
if (!$ret) {
    printf("[004] Blob data export failed. [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

if (!file_exists("lob_test.png")) {
    printf("[005] Blob data export error.\n");
    exit(1);
}

if (filesize("lob_test.png") != filesize("cubrid_logo.png")) {
    printf("[006] Blob data export error.\n");
    exit(1);
}

@unlink("lob_test.png");

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
done!
