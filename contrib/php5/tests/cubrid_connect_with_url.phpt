--TEST--
cubrid_connect_with_url
--SKIPIF--
--FILE--
<?php

include_once("connect.inc");

$tmp = NULL;
$conn = NULL;

if (!is_null($tmp = @cubrid_connect_with_url())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);
if (!$conn) {
    printf("[002] [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

$conn1 = cubrid_connect_with_url($connect_url, $user, $passwd, FALSE);
$conn2 = cubrid_connect_with_url($connect_url, $user, $passwd, TRUE);

if ($conn != $conn1) {
    printf("[003] The new_link parameter in cubrid_connect_with_url does not work!\n");
}

if ($conn == $conn2) {
    printf("[004] Can not make a new connection with the same parameters!");
}

cubrid_close($conn);
cubrid_close($conn2);

print "done!";
?>

<?php
$user = "public_error_user";
$passwd = "";
$connect_url = "CUBRID:$host:$port:$db:::";
$skip_on_connect_failure  = getenv("CUBRID_TEST_SKIP_CONNECT_FAILURE") ? getenv("CUBRID_TEST_SKIP_CONNECT_FAILURE") : true;

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);
if (!$conn) {
    printf("[005] [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
}



$user = "public";
$passwd = "wrong_password";
$connect_url = "CUBRID:$host:$port:$db:::";
$skip_on_connect_failure  = getenv("CUBRID_TEST_SKIP_CONNECT_FAILURE") ? getenv("CUBRID_TEST_SKIP_CONNECT_FAILURE") : true;

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);
if (!$conn) {
    printf("[006] [%d] %s\n", cubrid_error_code(), cubrid_error_msg());
}

?>


--CLEAN--
--EXPECTF--
done!

Warning: Error: DBMS, -165, User "%s" is invalid. in %s on line %d
[005] [-165] User "%s" is invalid.

Warning: Error: DBMS, -171, Incorrect or missing password. in %s on line %d
[006] [-171] Incorrect or missing password.
