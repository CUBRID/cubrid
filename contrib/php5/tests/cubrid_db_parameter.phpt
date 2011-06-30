--TEST--
cubrid_get_db_parameter() and cubrid_set_db_parameter()
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

if (!is_null($tmp = @cubrid_get_db_parameter())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

$params = cubrid_get_db_parameter($conn);

var_dump($params);

if (!is_null($tmp = @cubrid_set_db_parameter())) {
    printf("[002] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_set_db_parameter($conn, PARAM_ISOLATION_LEVEL))) {
    printf("[003] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (false !== ($tmp = @cubrid_set_db_parameter($conn, 10000, 1))) {
    printf("[004] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

cubrid_set_db_parameter($conn, CUBRID_PARAM_ISOLATION_LEVEL, 50);
cubrid_set_db_parameter($conn, CUBRID_PARAM_ISOLATION_LEVEL, 2);

cubrid_set_db_parameter($conn, CUBRID_PARAM_LOCK_TIMEOUT, 1);

$params_new = cubrid_get_db_parameter($conn);

var_dump($params_new);

cubrid_set_db_parameter($conn, CUBRID_PARAM_ISOLATION_LEVEL, $params['PARAM_ISOLATION_LEVEL']);
cubrid_set_db_parameter($conn, CUBRID_PARAM_LOCK_TIMEOUT, $params['PARAM_LOCK_TIMEOUT']);

$params_new = cubrid_get_db_parameter($conn);

while (list($param_key, $param_value) = each($params_new)) {
    if ($params[$param_key] != $param_value) {
        printf("[005] Expecting db parameter %s value %d, got %d\n", $param_key, $params[$param_key], $param_value);
    }
}

cubrid_close($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
array(4) {
  ["PARAM_ISOLATION_LEVEL"]=>
  int(3)
  ["PARAM_LOCK_TIMEOUT"]=>
  int(0)
  ["PARAM_MAX_STRING_LENGTH"]=>
  int(1073741823)
  ["PARAM_AUTO_COMMIT"]=>
  int(0)
}

Warning: Error: CCI, -25, Unknown transaction isolation level in %s on line %d
array(4) {
  ["PARAM_ISOLATION_LEVEL"]=>
  int(2)
  ["PARAM_LOCK_TIMEOUT"]=>
  int(1)
  ["PARAM_MAX_STRING_LENGTH"]=>
  int(1073741823)
  ["PARAM_AUTO_COMMIT"]=>
  int(0)
}
done!
