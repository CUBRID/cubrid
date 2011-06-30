<?php
echo "<html>";
echo "<title>cubrid_fetch_field</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_fetch_field</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "select * from test_table";

echo "Connecting to the database..."."<br>";

$cubrid_con = cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected."."<br>";

  echo "Executing: <b>".$sql."</b><br>";

  echo "Testing <b>cubrid_fetch_field</b>...<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
    echo "Information for column 0 (no column parameter given, uses 0):<br />\n";
    $meta = cubrid_fetch_field($result);
    if (!$meta) 
    {
        echo "No information available<br />\n";
    }
    else
    {
	    echo "<pre>
		max_length:		$meta->max_length
		multiple_key:		$meta->multiple_key
		name:			$meta->name
		not_null:		$meta->not_null
		numeric:		$meta->numeric
		table:			$meta->table
		type:			$meta->type
		default:		$meta->def
		unique_key:		$meta->unique_key
		</pre>";
	}
	
	echo "<br><br>Information for column 4 (column parameter given):<br />\n";
    $meta = cubrid_fetch_field($result, 4);
    if (!$meta) 
    {
        echo "No information available<br />\n";
    }
    else
    {
	    echo "<pre>
		max_length:		$meta->max_length
		multiple_key:		$meta->multiple_key
		name:			$meta->name
		not_null:		$meta->not_null
		numeric:		$meta->numeric
		table:			$meta->table
		type:			$meta->type
		default:		$meta->def
		unique_key:		$meta->unique_key
		</pre>";
	}

    cubrid_close_request($result); 
  }

  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>"."<br>";

  echo "<br>";
  echo "<b>Testing cubrid_fetch_field with wrong number of params: 0</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_fetch_field();
    cubrid_close_request($result); 
  }
  echo "<br>";
  
  echo "<br>";
  echo "<b>Testing cubrid_fetch_field with invalid value of column number: -1</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $meta = cubrid_fetch_field($result, -1);
    if (!$meta)
    	echo "Invalid column number<br>";
    cubrid_close_request($result); 
  }
  echo "<br>";
  
  echo "<br>";
  echo "<b>Testing cubrid_fetch_field with a large value of column number: 12345</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $meta = cubrid_fetch_field($result, 12345);
    if (!$meta)
    	echo "Invalid column number<br>";
    cubrid_close_request($result); 
  }

  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
