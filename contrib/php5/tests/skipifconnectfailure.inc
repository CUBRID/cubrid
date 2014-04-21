<?php
require_once('connect.inc');
if ($skip_on_connect_failure) {
	
    if (!$conn = @cubrid_connect($host, $port, $db, $user, $passwd)) {
        die(sprintf("skip Can't connect to CUBRID DB Server - [%d] %s", cubrid_errno(), cubrid_error()));
    }

    cubrid_disconnect($conn);
}
?>
