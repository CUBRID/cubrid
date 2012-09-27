--TEST--
cubrid_fetch_lengths
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc')
require_once('until.php')
?>
--FILE--
<?php

include_once("connect.inc");

$conn = cubrid_connect_with_url($connect_url, $user, $passwd);
if (!$conn) {
    printf("[001] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
    exit(1);
}

require_once('table.inc');

@cubrid_execute($conn, "INSERT INTO php_cubrid_test(a,d) VALUES (1, 'char1'), (2, 'varchar22')");

if (!$req = cubrid_execute($conn, "select * from php_cubrid_test; select * from code", CUBRID_EXEC_QUERY_ALL)) {
    printf("[002] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

$row = cubrid_fetch_row($req);
printf("The first row: %d %s\n", $row[0], $row[3]);

$lens = cubrid_fetch_lengths($req);
printf("Field lengths: ");
for ($i = 0; $i < 6; $i++) {
    printf("%d ", $lens[$i]);
}
printf("\n");

$row = cubrid_fetch_row($req);
printf ("The second row: %d %s\n", $row[0], $row[3]);

$lens = cubrid_fetch_lengths($req);
printf("Field lengths: ");
for ($i = 0; $i < 6; $i++) {
    printf("%d ", $lens[$i]);
}
printf("\n");

$row = cubrid_fetch_row($req);
$lens = cubrid_fetch_lengths($req);

if (!cubrid_next_result($req)) {
    printf("[003] [%d] %s\n", cubrid_errno($conn), cubrid_error($conn));
}

$row = cubrid_fetch_row($req);
printf ("\nThe third row: %s %s\n", $row[0], $row[1]);

$lens = cubrid_fetch_lengths($req);
printf("Field lengths: ");
for ($i = 0; $i < 2; $i++) {
    printf("%d ", $lens[$i]);
}
printf("\n");

cubrid_close_request($req);
cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
<?php
require_once("clean_table.inc");
?>
--EXPECTF--
The first row: 1 char1                         
Field lengths: 1 0 0 30 0 0 
The second row: 2 varchar22                     
Field lengths: 1 0 0 30 0 0 

Warning: Error: CLIENT, -30003, Cannot get column info in %s on line %d

The third row: X Mixed
Field lengths: 1 5 
done!
