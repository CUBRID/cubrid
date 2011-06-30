<?php
echo "<html>";
echo "<title>cubrid_get_charset</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_get_charset</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql="select charset from db_root";

echo "Connecting to the database..."."<br>";

$cubrid_con = @cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected."."<br>";

  echo "Testing <b>cubrid_get_charset</b>...<br>";

  echo "<br>";
  $result=cubrid_execute($cubrid_con, $sql);
  $row=cubrid_fetch_row($result);
  echo "Select charset from db_root returns: ".$row[0];
  echo "<br>";
  cubrid_close_request($result); 

  echo "<br>";
  echo "cubrid_get_charset returns: ".cubrid_get_charset($cubrid_con);
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_get_charset with wrong number of params: 0</b>...<br>";
  cubrid_get_charset();
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_get_charset with wrong number of params: 2</b>...<br>";
  cubrid_get_charset(1,1);
  echo "<br>";


  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
