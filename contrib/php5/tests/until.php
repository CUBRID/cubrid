<?php

function check_table_existence($conn_handle, $table_name)
{
    $sql_stmt = "SELECT class_name FROM db_class WHERE class_name = ?";
    $cubrid_req = cubrid_prepare($conn_handle, $sql_stmt);
    if (!$cubrid_req) {
	return -1;
    }

    $cubrid_retval = cubrid_bind($cubrid_req, 1, $table_name);
    if (!$cubrid_req) {
	cubrid_close_request($cubrid_req);
	return -1;
    }
     
    $cubrid_retval = cubrid_execute($cubrid_req);
    if (!$cubrid_retval) {
	cubrid_close_request($cubrid_req);
	return -1;
    }

    $row_num = cubrid_num_rows($cubrid_req);
    if ($row_num < 0) {
	cubrid_close_request($cubrid_req);
	return -1;
    }
    
    cubrid_close_request($cubrid_req);

    if ($row_num > 0) {
	return 1;
    } else {
	return 0;
    }
}
?>
