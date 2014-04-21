--TEST--
cubrid_lob2_import
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php

include_once('connect.inc');

$tmp = NULL;

$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (!is_null($tmp = @cubrid_lob2_import())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

@cubrid_execute($conn, 'DROP TABLE IF EXISTS test_lob2');
cubrid_execute($conn, 'CREATE TABLE test_lob2 (id INT AUTO_INCREMENT, images BLOB)');

$req = cubrid_prepare($conn, "INSERT INTO test_lob2(images) VALUES (?)");

$lob = cubrid_lob2_new($conn);

if (false !== ($tmp = @cubrid_lob2_import($lob, "file_not_exist"))) {
    printf("[002] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_lob2_import($lob, 'cubrid_logo.png');

if (filesize("cubrid_logo.png") != cubrid_lob2_size($lob)) {
    printf("[003] cubrid_lob2_import error, filesize is inconsistent.\n");
}

cubrid_lob2_bind($req, 1, $lob);

cubrid_execute($req);

cubrid_disconnect($conn);

print 'done!';
?>
--CLEAN--
--EXPECTF--
done!
