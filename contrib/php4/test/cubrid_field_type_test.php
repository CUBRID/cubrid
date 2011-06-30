<?php
echo "<html>";
echo "<title>cubrid_field_type</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_field_type</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "select column_integer, column_numeric_9_2, column_date from test_table";

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

  echo "Testing <b>cubrid_field_type</b>...<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
	echo "Field 0 (". cubrid_field_name($result, 0) . ") has type " . cubrid_field_type($result, 0) . "<br>";
	echo "Field 1 (". cubrid_field_name($result, 1) . ") has type " . cubrid_field_type($result, 1) . "<br>";
	echo "Field 2 (". cubrid_field_name($result, 2) . ") has type " . cubrid_field_type($result, 2) . "<br>";

    cubrid_close_request($result); 
  }

  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>"."<br>";

  echo "<br>";
  echo "<b>Testing cubrid_field_type with wrong number of params: 0</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_field_type();
    cubrid_close_request($result); 
  }
  echo "<br>";
  
  echo "<br>";
  echo "<b>Testing cubrid_field_type with invalid value of column number: -1</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_field_type($result, -1);
    cubrid_close_request($result); 
  }
  echo "<br>";
  
  echo "<br>";
  echo "<b>Testing cubrid_field_type with a large value of column number: 12345</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_field_type($result, 12345);
    cubrid_close_request($result); 
  }
  echo "<br>";


  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
