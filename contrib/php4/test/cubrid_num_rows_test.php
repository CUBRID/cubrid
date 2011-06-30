<?php
echo "<html>";
echo "<title>cubrid_num_rows</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_num_rows</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "select * from test_table_2";

echo "Connecting to the database..."."<br>";

$cubrid_con = @cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected.<br><br>";

  echo "Testing <b>cubrid_num_rows</b>...<br>";
  echo "<br>";
  echo "Executing: <b>".$sql."</b><br>";
  echo "<br>";

  $result = cubrid_execute($cubrid_con, $sql);

  if ($result) 
  {
    $value = cubrid_num_rows($result); 
    echo "Number of rows returned:";
    echo $value;
    echo "<br>";
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
