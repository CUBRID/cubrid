<?php
echo "<html>";
echo "<title>cubrid_fetch_assoc</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_fetch_assoc</b> implementation...";
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

  echo "Testing <b>cubrid_fetch_assoc</b>...<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
  	echo "Fetching fields of row 0: ";
	cubrid_data_seek($result, 0);
	$row = cubrid_fetch_assoc($result);
	echo $row["column_integer"] . " " . $row["column_numeric_9_2"]. " " . $row["column_date"] . "<br>";

  	echo "Fetching fields of row 2: ";	
	cubrid_data_seek($result, 2);
	$row = cubrid_fetch_assoc($result);
	echo $row["column_integer"] . " " . $row["column_numeric_9_2"]. " " . $row["column_date"] . "<br>";

    cubrid_close_request($result); 
  }

  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>"."<br>";

  echo "<br>";
  echo "<b>Testing cubrid_fetch_assoc with wrong number of params: 0</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $row = cubrid_fetch_assoc();
    cubrid_close_request($result); 
  }
  echo "<br>";
  echo "<br>";

  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
