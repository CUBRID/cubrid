--TEST--
cubrid_lob2_bind
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

if (!is_null($tmp = @cubrid_lob2_bind())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

@cubrid_execute($conn, 'DROP TABLE IF EXISTS test_blob');
cubrid_execute($conn, 'CREATE TABLE test_blob (id INT, contents BLOB)');

// The default type that cubrid_lob2_new will create is BLOB.

$req = cubrid_prepare($conn, 'INSERT INTO test_blob VALUES (?, ?)');

if (false !== ($tmp = cubrid_lob2_bind($req, 10, 'test'))) {
    printf('[002] Expecting boolean/false, got %s/%s\n', gettype($tmp), $tmp);
}

$lob = cubrid_lob2_new();

cubrid_bind($req, 1, 1);
cubrid_lob2_bind($req, 2, $lob);

cubrid_execute($req);

if (false !== ($tmp = cubrid_lob2_bind($req, 2, $lob, 'CLOB'))) {
    printf("[003] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if (false !== ($tmp = cubrid_lob2_bind($req, 2, 10, 'INT'))) {
    printf("[004] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_disconnect($conn);

print 'done!';
?>
--CLEAN--
--EXPECTF--
Warning: cubrid_lob2_bind(): Wrong type, the type you create is not CLOB. in %s on line %d

Warning: cubrid_lob2_bind(): This function only can be used to bind BLOB/CLOB. in %s on line %d
done!
