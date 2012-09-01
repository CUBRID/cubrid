--TEST--
cubrid_lob2_read
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

if (!is_null($tmp = @cubrid_lob2_read())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

$lob = cubrid_lob2_new($conn, 'CLOB');

cubrid_lob2_write($lob, "Hello, welcome to CUBRID world! I'm LOB.");
$pos = cubrid_lob2_tell($lob);
print "LOB positon after written is $pos\n";

cubrid_lob2_seek($lob, 0, CUBRID_CURSOR_FIRST);
$str = cubrid_lob2_read($lob, 30);
print "read 30 characters from lob: $str\n";

$pos = cubrid_lob2_tell($lob);
print "LOB positon after read 30 character is $pos\n";

$str = cubrid_lob2_read($lob, 20);
print "read 20 characters from lob: $str\n";

$pos = cubrid_lob2_tell($lob);
print "LOB positon after read 20 characters is $pos\n";

if (false !== ($tmp = cubrid_lob2_read($lob, 10))) {
    printf("[002] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_disconnect($conn);
print 'done!';
?>
--CLEAN--
--EXPECTF--
LOB positon after written is 40
read 30 characters from lob: Hello, welcome to CUBRID world
LOB positon after read 30 character is 30
read 20 characters from lob: ! I'm LOB.
LOB positon after read 20 characters is 40
done!
