--TEST--
cubrid_lob2_export
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

if (!is_null($tmp = @cubrid_lob2_export())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

@cubrid_execute($conn, 'DROP TABLE IF EXISTS test_lob2');
cubrid_execute($conn, 'CREATE TABLE test_lob2 (id INT AUTO_INCREMENT, images BLOB, contents CLOB)');

prepare_table_for_test($conn);

$req = cubrid_execute($conn, 'select * from test_lob2');

while ($row = cubrid_fetch_row($req, CUBRID_LOB)) {
    cubrid_lob2_export($row[1], "image_" . $row[0] . ".png");
    cubrid_lob2_export($row[2], "content_" . $row[0] . ".txt");

    if (!file_exists("image_" . $row[0] . ".png")) {
        printf("[002] BLOB data export error.\n");
    }

    if (!file_exists("content_" . $row[0] . ".txt")) {
        printf("[003] CLOB data export error.\n");
    }

    @unlink("image_" . $row[0] . ".png");
    @unlink("content_" . $row[0] . ".txt");
}

cubrid_move_cursor($req, 1, CUBRID_CURSOR_FIRST);
$row = cubrid_fetch_row($req, CUBRID_LOB);

cubrid_lob2_export($row[1], "test_cubrid_lob2_export");

if (false !== ($tmp = cubrid_lob2_export($row[2], "test_cubrid_lob2_export"))) {
    printf("[004] Expecting boolean/false, got %s/%s\n", gettype($tmp), $tmp);
}

@unlink("test_cubrid_lob2_export");

cubrid_disconnect($conn);

print 'done!';

function prepare_table_for_test($conn) {

    $req = cubrid_prepare($conn, 'INSERT INTO test_lob2(images, contents) VALUES (?, ?)');
    
    $lob_1 = cubrid_lob2_new($conn, 'BLOB');
    cubrid_lob2_import($lob_1, 'cubrid_logo.png');
    cubrid_lob2_bind($req, 1, $lob_1);
    
    $lob_2 = cubrid_lob2_new($conn, 'CLOB');
    cubrid_lob2_import($lob_2, 'connect.inc');
    cubrid_lob2_bind($req, 2, $lob_2);
    
    cubrid_execute($req);
    
    $lob_1 = cubrid_lob2_new($conn, 'BLOB');
    cubrid_lob2_import($lob_1, 'cubrid_logo.png');
    cubrid_lob2_bind($req, 1, $lob_1);
    
    
    $lob_2 = cubrid_lob2_new($conn, 'CLOB');
    cubrid_lob2_import($lob_2, 'table.inc');
    cubrid_lob2_bind($req, 2, $lob_2);
    
    cubrid_execute($req);
    
    $lob_1 = cubrid_lob2_new($conn, 'BLOB');
    cubrid_lob2_import($lob_1, 'cubrid_logo.png');
    cubrid_lob2_bind($req, 1, $lob_1);
    
    
    $lob_2 = cubrid_lob2_new($conn, 'CLOB');
    cubrid_lob2_import($lob_2, 'clean_table.inc');
    cubrid_lob2_bind($req, 2, $lob_2);
    
    cubrid_execute($req);

    cubrid_close_request($req);
}

?>
--CLEAN--
--EXPECTF--
Warning: cubrid_lob2_export(): The file that you want to export lob object may have existed. in %s on line %d
done!
