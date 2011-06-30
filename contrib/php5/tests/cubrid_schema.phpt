--TEST--
cubrid_schema()
--SKIPIF--
<?php
require_once('skipif.inc');
require_once('skipifconnectfailure.inc');
?>
--FILE--
<?php
include "connect.inc";

$tmp = NULL;
$conn = NULL;

if (!is_null($tmp = @cubrid_schema())) {
    printf("[001] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_schema($conn))) {
    printf("[002] Expecting NULL, got %s/%s\n", gettype($tmp), $tmp);
}

if (!$conn = cubrid_connect($host, $port, $db,  $user, $passwd)) {
    printf("[003] Cannot connect to db server using host=%s, port=%d, dbname=%s, user=%s, passwd=***\n",
    $host, $port, $db, $user);
}

if (($schema = cubrid_schema($conn, 1000)) !== false) {
    printf("[004] Expecting false, got %s/%s\n", gettype($schema), $schema);
}

if (($schema = cubrid_schema($conn, CUBRID_SCH_PRIMARY_KEY, "game")) === false) {
    printf("[005] Cannot get schema type CUBRID_SCH_PRIMARY_KEY when table name is \"game\"\n");
}
var_dump($schema);

if (($schema = cubrid_schema($conn, CUBRID_SCH_IMPORTED_KEYS, "game")) === false) {
    printf("[006] Cannot get schema type CUBRID_SCH_IMPORTED_KEYS when table name is \"game\"\n");
}
var_dump($schema);

if (($schema = cubrid_schema($conn, CUBRID_SCH_EXPORTED_KEYS, "event")) === false) {
    printf("[007] Cannot get schema type CUBRID_SCH_EXPORTED_KEYS when table name is \"event\", error: [%d]:%s\n", 
            cubrid_error_code(), cubrid_error_msg());
}
var_dump($schema);

if (($schema = cubrid_schema($conn, CUBRID_SCH_CROSS_REFERENCE, "event", "game")) === false) {
    printf("[008] Cannot get schema type CUBRID_SCH_EXPORTED_KEYS when table name is \"event\", error: [%d]:%s\n", 
            cubrid_error_code(), cubrid_error_msg());
}
var_dump($schema);

print "done!";
?>
--CLEAN--
--EXPECTF--

Warning: Error: CAS, -1015, Invalid T_CCI_SCH_TYPE value in %s on line %d
array(3) {
  [0]=>
  array(4) {
    ["CLASS_NAME"]=>
    string(4) "game"
    ["ATTR_NAME"]=>
    string(12) "athlete_code"
    ["KEY_SEQ"]=>
    string(1) "3"
    ["KEY_NAME"]=>
    string(41) "pk_game_host_year_event_code_athlete_code"
  }
  [1]=>
  array(4) {
    ["CLASS_NAME"]=>
    string(4) "game"
    ["ATTR_NAME"]=>
    string(10) "event_code"
    ["KEY_SEQ"]=>
    string(1) "2"
    ["KEY_NAME"]=>
    string(41) "pk_game_host_year_event_code_athlete_code"
  }
  [2]=>
  array(4) {
    ["CLASS_NAME"]=>
    string(4) "game"
    ["ATTR_NAME"]=>
    string(9) "host_year"
    ["KEY_SEQ"]=>
    string(1) "1"
    ["KEY_NAME"]=>
    string(41) "pk_game_host_year_event_code_athlete_code"
  }
}
array(2) {
  [0]=>
  array(9) {
    ["PKTABLE_NAME"]=>
    string(7) "athlete"
    ["PKCOLUMN_NAME"]=>
    string(4) "code"
    ["FKTABLE_NAME"]=>
    string(4) "game"
    ["FKCOLUMN_NAME"]=>
    string(12) "athlete_code"
    ["KEY_SEQ"]=>
    string(1) "1"
    ["UPDATE_RULE"]=>
    string(1) "1"
    ["DELETE_RULE"]=>
    string(1) "1"
    ["FK_NAME"]=>
    string(20) "fk_game_athlete_code"
    ["PK_NAME"]=>
    string(15) "pk_athlete_code"
  }
  [1]=>
  array(9) {
    ["PKTABLE_NAME"]=>
    string(5) "event"
    ["PKCOLUMN_NAME"]=>
    string(4) "code"
    ["FKTABLE_NAME"]=>
    string(4) "game"
    ["FKCOLUMN_NAME"]=>
    string(10) "event_code"
    ["KEY_SEQ"]=>
    string(1) "1"
    ["UPDATE_RULE"]=>
    string(1) "1"
    ["DELETE_RULE"]=>
    string(1) "1"
    ["FK_NAME"]=>
    string(18) "fk_game_event_code"
    ["PK_NAME"]=>
    string(13) "pk_event_code"
  }
}
array(1) {
  [0]=>
  array(9) {
    ["PKTABLE_NAME"]=>
    string(5) "event"
    ["PKCOLUMN_NAME"]=>
    string(4) "code"
    ["FKTABLE_NAME"]=>
    string(4) "game"
    ["FKCOLUMN_NAME"]=>
    string(10) "event_code"
    ["KEY_SEQ"]=>
    string(1) "1"
    ["UPDATE_RULE"]=>
    string(1) "1"
    ["DELETE_RULE"]=>
    string(1) "1"
    ["FK_NAME"]=>
    string(18) "fk_game_event_code"
    ["PK_NAME"]=>
    string(13) "pk_event_code"
  }
}
array(1) {
  [0]=>
  array(9) {
    ["PKTABLE_NAME"]=>
    string(5) "event"
    ["PKCOLUMN_NAME"]=>
    string(4) "code"
    ["FKTABLE_NAME"]=>
    string(4) "game"
    ["FKCOLUMN_NAME"]=>
    string(10) "event_code"
    ["KEY_SEQ"]=>
    string(1) "1"
    ["UPDATE_RULE"]=>
    string(1) "1"
    ["DELETE_RULE"]=>
    string(1) "1"
    ["FK_NAME"]=>
    string(18) "fk_game_event_code"
    ["PK_NAME"]=>
    string(13) "pk_event_code"
  }
}
done!
