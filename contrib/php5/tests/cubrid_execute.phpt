--TEST--
cubrid_execute()
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include_once('connect.inc');

$tmp = NULL;
$conn = NULL;

if (!is_null($tmp = @cubrid_execute())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_execute($conn))) {
    printf('[002] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (false !== ($tmp = cubrid_execute($conn, 'THIS IS NOT SQL'))) {
    printf("[003] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

if (!($req = cubrid_execute($conn, 'SELECT * FROM code'))) {
    printf('[004] [%d] %s\n', cubrid_error_code(), cubrid_error_msg());
}

while ($res = cubrid_fetch_array($req, CUBRID_NUM)) {
    var_dump($res);
}

cubrid_close_request($req);

if (!$req = cubrid_prepare($conn, "SELECT * FROM code WHERE s_name = ?")) {
    printf('[005] [%d] %s\n', cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

if (false !== ($tmp = cubrid_execute($req))) {
    printf('[006] [%d] Expecting boolean/false, got %s/%s\n', gettype($tmp), $tmp);
}

if (!$req = cubrid_prepare($conn, "SELECT * FROM code WHERE s_name='M'")) {
    printf('[007] [%d] %s\n', cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

if (!($res = cubrid_execute($req))) {
    printf('[008] [%d] %s\n', cubrid_error_code(), cubrid_error_msg());
}

while ($array = cubrid_fetch_array($req)) {
    var_dump($array);
}

cubrid_close_request($req);
cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--

Warning: Error: DBMS, -493, Syntax: syntax error, unexpected IdName  in %s on line %d
array(2) {
  [0]=>
  string(1) "X"
  [1]=>
  string(5) "Mixed"
}
array(2) {
  [0]=>
  string(1) "W"
  [1]=>
  string(5) "Woman"
}
array(2) {
  [0]=>
  string(1) "M"
  [1]=>
  string(3) "Man"
}
array(2) {
  [0]=>
  string(1) "B"
  [1]=>
  string(6) "Bronze"
}
array(2) {
  [0]=>
  string(1) "S"
  [1]=>
  string(6) "Silver"
}
array(2) {
  [0]=>
  string(1) "G"
  [1]=>
  string(4) "Gold"
}

Warning: Error: CLIENT, -2015, Some parameter not binded in %s on line %d
array(4) {
  [0]=>
  string(1) "M"
  ["s_name"]=>
  string(1) "M"
  [1]=>
  string(3) "Man"
  ["f_name"]=>
  string(3) "Man"
}
done!
