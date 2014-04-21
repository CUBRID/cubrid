--TEST--
cubrid_fetch_array
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

if (!is_null($tmp = @cubrid_fetch_array())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_fetch_array($conn))) {
    printf("[002] Expecting false, got %s/%s\n", gettype($tmp), $tmp);
}

$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (!$req = cubrid_query("SELECT * FROM code", $conn)) {
	printf("[003] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

while ($array = cubrid_fetch_array($req)) {
    var_dump($array);
}

cubrid_move_cursor($req, 1, CUBRID_CURSOR_FIRST);

$array = cubrid_fetch_array($req, CUBRID_NUM);
var_dump($array);

$array = cubrid_fetch_array($req, CUBRID_ASSOC);
var_dump($array);

cubrid_close($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
array(4) {
  [0]=>
  string(1) "X"
  ["s_name"]=>
  string(1) "X"
  [1]=>
  string(5) "Mixed"
  ["f_name"]=>
  string(5) "Mixed"
}
array(4) {
  [0]=>
  string(1) "W"
  ["s_name"]=>
  string(1) "W"
  [1]=>
  string(5) "Woman"
  ["f_name"]=>
  string(5) "Woman"
}
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
array(4) {
  [0]=>
  string(1) "B"
  ["s_name"]=>
  string(1) "B"
  [1]=>
  string(6) "Bronze"
  ["f_name"]=>
  string(6) "Bronze"
}
array(4) {
  [0]=>
  string(1) "S"
  ["s_name"]=>
  string(1) "S"
  [1]=>
  string(6) "Silver"
  ["f_name"]=>
  string(6) "Silver"
}
array(4) {
  [0]=>
  string(1) "G"
  ["s_name"]=>
  string(1) "G"
  [1]=>
  string(4) "Gold"
  ["f_name"]=>
  string(4) "Gold"
}
array(2) {
  [0]=>
  string(1) "X"
  [1]=>
  string(5) "Mixed"
}
array(2) {
  ["s_name"]=>
  string(1) "W"
  ["f_name"]=>
  string(5) "Woman"
}
done!
