--TEST--
cubrid_fetch_object
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

if (!is_null($tmp = @cubrid_fetch_object())) {
    printf('[001] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

if (!is_null($tmp = @cubrid_fetch_object($conn))) {
    printf('[002] Expecting NULL, got %s/%s\n', gettype($tmp), $tmp);
}

$conn = cubrid_connect($host, $port, $db, $user, $passwd);

if (!($res = cubrid_execute($conn, "SELECT * FROM code limit 5"))) {
    printf('[003] [%d] %s\n', cubrid_error_code(), cubrid_error_msg());
    exit(1);
}

var_dump(cubrid_fetch_object($res));

class cubrid_fetch_object_test {
    public $s_name = NULL;
    public $f_name = NULL;

    public function toString() {
        var_dump($this);
    }
}

var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_test'));

class cubrid_fetch_object_construct extends cubrid_fetch_object_test {

	public function __construct($s, $f) {
		$this->s_name = $s;
		$this->f_name = $f;
	}

}

var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_construct', null));
var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_construct', array('s_name')));
var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_construct', array('s_name', 'f_name')));
var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_construct', array('s_name', 'f_name', 'x')));
var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_construct', "no array and not null"));
var_dump(cubrid_fetch_object($res));
var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_construct', array('s_name', 'f_name')));

class cubrid_fetch_object_private_construct {

	private function __construct($s, $f) {
		var_dump($s);
	}

}

var_dump(cubrid_fetch_object($res, 'cubrid_fetch_object_private_construct', array('s_name', 'f_name')));

// Fatal error, script execution will end
var_dump(cubrid_fetch_object($res, 'this_class_does_not_exist'));

cubrid_disconnect($conn);

print "done!";
?>
--CLEAN--
--EXPECTF--
object(stdClass)#%d (2) {
  ["s_name"]=>
  string(1) "X"
  ["f_name"]=>
  string(5) "Mixed"
}
object(cubrid_fetch_object_test)#%d (2) {
  ["s_name"]=>
  string(1) "W"
  ["f_name"]=>
  string(5) "Woman"
}

Warning: Missing argument 1 for cubrid_fetch_object_construct::__construct() in %s on line %d

Warning: Missing argument 2 for cubrid_fetch_object_construct::__construct() in %s on line %d

Notice: Undefined variable: s in %s on line %d

Notice: Undefined variable: f in %s on line %d
object(cubrid_fetch_object_construct)#%d (2) {
  ["s_name"]=>
  NULL
  ["f_name"]=>
  NULL
}

Warning: Missing argument 2 for cubrid_fetch_object_construct::__construct() in %s on line %d

Notice: Undefined variable: f in %s on line %d
object(cubrid_fetch_object_construct)#%d (2) {
  ["s_name"]=>
  string(6) "s_name"
  ["f_name"]=>
  NULL
}
object(cubrid_fetch_object_construct)#%d (2) {
  ["s_name"]=>
  string(6) "s_name"
  ["f_name"]=>
  string(6) "f_name"
}
bool(false)
bool(false)
bool(false)
bool(false)
bool(false)

Fatal error: Class 'this_class_does_not_exist' not found in %s on line %d
