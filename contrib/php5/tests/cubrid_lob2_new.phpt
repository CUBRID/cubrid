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

cubrid_execute($conn, 'CREATE TABLE test_lob2 (id INT, images BFILE, contents CFILE)');

$req = cubrid_prepare($conn, 'INSERT INTO test_lob2 VALUES (?, ?, ?)');

cubrid_bind($req, 1, 1);

// The default type that cubrid_lob2_new will create is BFILE.
$lob_bfile = cubrid_lob2_new();
cubrid_lob2_bind($req, 2, $lob_bfile);

// If you want to create a CFILE data, you must give 'cfile' to the type parameter.
$lob_cfile = cubrid_lob2_new($conn, 'cfile');
cubrid_lob2_bind($req, 3, $lob_cfile);

cubrid_execute($req);

$req = cubrid_prepare($conn, 'INSERT INTO test_lob2 (images) VALUES (?)');

$lob_bfile_2 = cubrid_lob2_new($conn);
cubrid_lob2_bind($req, 1, $lob_bfile_2);

cubrid_execute($req);

$lob_bfile_3 = cubrid_lob2_new($conn, 'BFILE');
cubrid_lob2_bind($req, 1, $lob_bfile_3);

cubrid_execute($req);

cubrid_disconnect($conn);

print 'done!';
?>
--CLEAN--
--EXPECTF--
done!
