--TEST--
cubrid_query
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include_once("connect.inc");

$tmp = NULL;
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (!is_null($tmp = @cubrid_query())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (NULL !== ($tmp = @cubrid_query("SELECT 1 AS a", $conn, "code"))) {
    printf("[002] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (false !== ($tmp = cubrid_query('THIS IS NOT SQL', $conn))) {
    printf("[003] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if ((0 === cubrid_errno($conn)) || ('' == cubrid_error($conn))) {
    printf("[004] cubrid_errno()/cubrid_error should return some error\n");
}

if (!$res = cubrid_query("SELECT 'this is sql but with semicolon' AS valid ; ", $conn)) {
    printf("[005] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

var_dump(cubrid_fetch_assoc($res));
cubrid_free_result($res);

cubrid_close($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
Warning: Error: DBMS, -493, Syntax: syntax error, unexpected IdName  in %s on line %d
array(1) {
  ["valid"]=>
  string(30) "this is sql but with semicolon"
}
done!
