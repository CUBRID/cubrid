--TEST--
cubrid_lob2_seek
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

if (!is_null($tmp = @cubrid_lob2_seek())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

@cubrid_execute($conn, 'DROP TABLE IF EXISTS test_lob2');
cubrid_execute($conn, 'CREATE TABLE test_lob2 (id INT, contents CLOB)');

$req = cubrid_prepare($conn, 'INSERT INTO test_lob2 VALUES (?, ?)');

cubrid_bind($req, 1, 10);
cubrid_lob2_bind($req, 2, "Wow, welcome to CUBRID! You are using CLOB now!", "CLOB");

cubrid_execute($req);

$req = cubrid_execute($conn, "SELECT * FROM test_lob2");

$row = cubrid_fetch_row($req, CUBRID_LOB);

$lob = $row[1];

$size = cubrid_lob2_size($lob);

print "cubrid_lob2_size : $size\n";

cubrid_lob2_seek($lob, $size, CUBRID_CURSOR_FIRST);

$position = cubrid_lob2_tell($lob);
print "position after move $size related to CUBRID_CURSOR_FIRST: $position\n";

if (false !== ($tmp = cubrid_lob2_seek($lob, $size + 1, CUBRID_CURSOR_FIRST))) {
    printf("[002] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_lob2_seek($lob, $size, CUBRID_CURSOR_LAST);

$position = cubrid_lob2_tell($lob);
print "position after move $size related to CUBRID_CURSOR_LAST: $position\n";

if (false !== ($tmp = cubrid_lob2_seek($lob, -1 , CUBRID_CURSOR_CURRENT))) {
    printf("[003] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_lob2_seek($lob, $size - 20, CUBRID_CURSOR_CURRENT);

$position = cubrid_lob2_tell($lob);
print "position after move " . ($size - 20) . " related to CUBRID_CURSOR_CURRENT: $position\n";

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
cubrid_lob2_size : 47
position after move 47 related to CUBRID_CURSOR_FIRST: 47

Warning: cubrid_lob2_seek(): offset(48) is not correct, it can't be a negative number or larger than size in %s on line %d
position after move 47 related to CUBRID_CURSOR_LAST: 0

Warning: cubrid_lob2_seek(): offet(-1) is out of range, please input a proper number. in %s on line %d
position after move 27 related to CUBRID_CURSOR_CURRENT: 27
done!
