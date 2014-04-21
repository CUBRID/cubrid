--TEST--
cubrid_bind
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
require_once('until.php')
?>
--FILE--
<?php
include_once('connect.inc');

$tmp = NULL;
$conn = cubrid_connect($host, $port, $db, $user, $passwd);

@cubrid_execute($conn, 'DROP TABLE bind_test');
cubrid_execute($conn, 'CREATE TABLE bind_test(c1 varchar(10))');

$req = cubrid_prepare($conn, 'INSERT INTO bind_test(c1) VALUES(?)');

cubrid_bind($req, 1, null);
cubrid_execute($req);

cubrid_bind($req, 1, '1234');
cubrid_execute($req);

cubrid_bind($req, 1, null, "null");
cubrid_execute($req);

$req = cubrid_execute($conn, "SELECT * FROM bind_test");
while ($row = cubrid_fetch_assoc($req)) {
    if ($row["c1"]) {
        printf("%s\n", $row["c1"]);
    } else {
        printf("NULL\n");    
    }
}

print 'done!';
?>
--CLEAN--
<?php
require_once("clean_table.inc");
?>
--EXPECTF--
NULL
1234
NULL
done!
