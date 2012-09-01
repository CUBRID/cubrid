--TEST--
cubrid_lob2_new
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

if (false !== ($tmp = @cubrid_lob2_new($conn, 'NULL'))) {
    printf('[001] Expecting boolean/false, got %s/%s\n', gettype($tmp), $tmp);
}

@cubrid_execute($conn, 'DROP TABLE IF EXISTS test_lob2');

cubrid_execute($conn, 'CREATE TABLE test_lob2 (id INT, images BLOB, contents CLOB)');

$req = cubrid_prepare($conn, 'INSERT INTO test_lob2 VALUES (?, ?, ?)');

cubrid_bind($req, 1, 1);

// The default type that cubrid_lob2_new will create is BLOB.
$lob_blob = cubrid_lob2_new();
cubrid_lob2_bind($req, 2, $lob_blob);

// If you want to create a CLOB data, you must give 'clob' to the type parameter.
$lob_clob = cubrid_lob2_new($conn, 'clob');
cubrid_lob2_bind($req, 3, $lob_clob);

cubrid_execute($req);

$req = cubrid_prepare($conn, 'INSERT INTO test_lob2 (images) VALUES (?)');

$lob_blob_2 = cubrid_lob2_new($conn);
cubrid_lob2_bind($req, 1, $lob_blob_2);

cubrid_execute($req);

$lob_blob_3 = cubrid_lob2_new($conn, 'BLOB');
cubrid_lob2_bind($req, 1, $lob_blob_3);

cubrid_execute($req);

cubrid_disconnect($conn);

print 'done!';
?>
--CLEAN--
--EXPECTF--
done!
